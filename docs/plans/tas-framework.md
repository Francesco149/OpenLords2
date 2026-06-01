# TAS framework — deterministic anchors for port↔retail parity (plan)

> Direction set 2026-06-01 (user): *"flesh out the TAS harness in the same
> style as ../openrecet. We want deterministic anchors across both retail and
> port where the same input trace produces the same frames, so we can 1:1 bit
> compare deterministically. Set up a trace that captures the first frame after
> dismissing the dialogues on a new game; implement any unported things along
> the way to reach deterministic renders up to that frame. Then build up from
> there, porting more things while grounded in truth."*

This is the OpenLords2 port of `../openrecet/docs/plans/tas-framework.md`
(read that for the fully-worked rationale; this doc only records what differs
for Lords of the Realm 2 and the concrete phase plan here).

## Design principle: the trace is the source of truth

One TAS trace must drive the port and retail **identically — assuming the port
is accurate**. The input trace (+ a pinned RNG seed and clock) is the single
source of truth; a faithful port is a deterministic function of it, bit-for-bit
in lockstep with retail. Two consequences:

1. **Anchors are a correctness signal, not just an alignment crutch.** The same
   trace makes a named event (`MAP_FREEROAM`) fire at some frame on each side.
   The gap between the two frames *localises* the first divergence — a port
   inaccuracy or a determinism leak. As the port becomes accurate, anchor
   frames converge; in the limit the per-side anchor remap is the identity and
   the trace alone suffices.
2. **Determinism must be pinned before "identical behaviour" is testable.** The
   same trace behaves identically only if every non-input source of variation
   is pinned on both sides: **RNG seed**, **clock/QPC** (turbo already pins
   `timeGetTime` to a fixed virtual step), and frame-step.

## The OpenLords2 reality (what differs from openrecet)

openrecet's port is a runnable native exe with its own render loop, so a trace
drives both targets and you frame-diff directly. OpenLords2 is **partway there**:

- **The drop-in (`build/openlords2.exe`) runs and renders the title menu 1:1**
  vs retail (`--title-live`/`--title-nav`, 0-diff — see
  [project_menu_navigation]). It has a real `WinMain` + main loop
  (`src/main.c`) and a per-tick driver scaffold (`engine.c`, the FUN_004b99c0
  analog) with a `g_frame_counter`.
- **But it cannot yet render an in-game strategic-map frame.** Two walls:
  (a) the **new-game initialiser** `FUN_00497ced` is only partway ported (its
  sub-orchestrator `FUN_004676e0` is now leaf-complete, but `FUN_00497ced`
  itself isn't assembled and several heavy seeders remain — see
  `docs/findings/game-start.md`); and (b) the **strategic-map screen-dispatch
  arm** (`FUN_0040f5fd`, screen-id 0) is still `other_stub` in
  `engine_screen_dispatch.c`, so even with a populated world the drop-in draws
  nothing for the map. The map-tile renderer cluster *is* ported
  (`engine_map_render.c` / `engine_map_terrain.c`) but only validated offscreen
  via the scene-composition harness (`tools/scene_render_parity.py`), never
  driven through the live loop.

**Therefore a *port-side* frame of "new game after dialogs" is several porting
sessions away.** What this framework delivers now, and why it's the right
foundation:

- A **deterministic retail ground-truth capture** of the target frame (the
  thing the user asked for) — reproducible bit-for-bit across replays.
- **Anchors on both targets.** The port emits the anchors it *can* reach
  (`BOOT`, `TITLE`, and — as porting lands — `NEW_GAME`/`LOADING`/`MAP`). The
  anchor stream then **names the exact frame where the port diverges** from the
  deterministic retail timeline. That divergence point *is* the prioritised
  porting frontier, measured instead of guessed.
- The **segtrace + watch + determinism** substrate so every future render chip
  (the map, the HUD, combat) is verified against a frozen deterministic state
  instead of a hand-driven one.

"Build up from there" = port `FUN_00497ced` + the screen-dispatch map arm until
the port's `MAP_FREEROAM` anchor fires and its frame converges on the captured
truth.

## Anchor set (OpenLords2)

Keyed on the shared per-tick counter — retail `g_tick_count` (agent, bumped in
`engine_tick.onEnter` @ FUN_004b99c0) ≡ port `g_frame_counter` (main.c, bumped
at the same per-tick boundary). Both read the same two state globals:

| anchor | fires when | globals |
|--------|-----------|---------|
| `BOOT` | first tick | — |
| `TITLE` | `screen_id == 0x1f` | `DAT_004eac50` (`var_sim_current_screen`) |
| `NEW_GAME` | `engine_state` → 3 (INGAME) leaving title | `DAT_0052af94` (`var_sim_game_state_phase`) |
| `LOADING_START`/`_END` | `engine_state == 0xb` edges | `DAT_0052af94` |
| `MAP_FREEROAM` | `engine_state == 3 && screen_id == 0` (strategic map live, not loading) | both |
| `SCROLL_DISMISSED` | message-scroll/popup count → 0 at the map | *scroll-active global — TODO, RE pending* |

`engine_state` (`DAT_0052af94`) values seen so far: 1 = boot intro movie, 3 =
INGAME, 0xa = saving, 0xb = loading, `>8` = fast-skip render (main-loop-tick.md).
`screen_id` (`DAT_004eac50`): 0x1f = title, 0 = strategic map / menu,
0x28..0x2a = loading screens.

The `SCROLL_DISMISSED` anchor needs the in-game message-scroll subsystem's
"active" global, which is **not yet RE'd** (the objectives / "Non 256 colour"
scrolls that overlay the first map frame). Until then the dismiss is driven by
the proven spray-click column (`tools/traces/load_map_clean.jsonl`) plus a
`{wait MAP_FREEROAM}` + fixed offset; upgrading to `{wait_until scroll==0}`
once the global is found removes the last frame-count guess.

## Anchor-segmented input trace (`--input-segtrace`)

A superset of the current sparse `{t,m,w,l}` replay (`--input-trace`). New ops
as JSONL lines:

- `{"wait":"ANCHOR"}` — segment break. Hold/repeat nothing; consume zero
  counted frames until the named anchor fires, then **rebase** the segment's
  frame-0 onto that anchor's frame. Spans non-deterministic loads.
- `{"capture":N}` — capture a frame `N` ticks after the current segment's
  anchor (anchor-relative, immune to load jitter). Offset 0 = the anchor frame.
- `{"wait_until":{"va":"0x..","type":"u8|u32|i32|f32","op":"<=|>=|<|>|==|!=","val":V}}`
  — hold the current input until a live-global predicate holds, then rebase the
  next segment. Removes frame-count guessing for "stop when state X".

Within a segment, plain `{t,m,w,l}` lines are the existing format, with `t`
measured **relative to the segment's opening anchor**. Backward compatible: a
trace with no `wait`/`capture`/`wait_until` line is a legacy absolute trace and
replays exactly as today.

This subsumes the scattered capture flags (`--frame-capture-warmup-secs`,
`--input-trace`, the spray-and-pray dismiss) into one declarative surface, as
openrecet's did. Retail side lands first (drivable end-to-end now); the port-C
mirror (`src/input_segtrace.{c,h}`) follows when the port can reach the
segments past `TITLE`.

## Determinism pinning

- **Clock:** turbo already pins `timeGetTime` to `g_turbo_step_ms` per tick on
  the main thread — virtual time is reproducible. (Verified for the diff
  harness.)
- **RNG:** a new game **generates the map from the RNG**, so the seed must be
  pinned for the frame to be reproducible. Lords2 has two LFSRs: the double-LFSR
  `FUN_00404a46` (`engine_rng_step`, state `DAT_0058fe28`/`DAT_0058fe08`,
  seeded at WinMain) and the single-LFSR `FUN_00404b2c` (`engine_rng_step_single`,
  state `DAT_00591564`, seeded `0x34518632` at InitInstance) that the new-game
  grid randomisers thread. `--rng-seed S` writes both seed states after init
  but before the new-game transition consumes them, so the generated map is a
  function of `S` + the trace.
- **`--watch NAME=0xVA:type`** emits the chosen globals every tick to
  `watch.jsonl` — the per-frame state oracle (subsumes ad-hoc mem probes;
  feeds `wait_until` authoring + divergence diagnosis).

## Phased implementation

- **P1 — retail anchors + capture. ✅ DONE (2026-06-01).** `anchorTick()` in the
  agent emits `{kind:"anchor",anchor,frame}` on engine-state/screen-id edges;
  `frida_capture.py --anchor-trace` → `anchors.jsonl`. Validated live.
- **P2 — `--input-segtrace` (retail). ✅ DONE (2026-06-01).**
  `{wait}`/`{wait_until}`/`{capture}` ops; anchor-relative capture; `wait`
  resolves against a per-segment floor (`g_seg_wait_floor`) so a fast anchor
  that fires while prior inputs are still dispatching still satisfies it;
  `segtrace_done` ends the run deterministically. Validated + bit-identical
  across replays.
- **P3 — determinism pins + watch. ✅ DONE (2026-06-01).** `--rng-seed` pins the
  three LFSR states each pre-ingame tick (the new-game map-gen is synchronous —
  consumes the seed in the title→ingame tick); `--watch NAME=0xVA:type`
  edge-emits a state oracle. **Finding:** under turbo the run is already
  bit-deterministic within a target; `--rng-seed` is needed for port↔retail
  (the two targets don't seed from the same wall-clock instant). Confirmed it
  changes the map vs the menu-advanced state, pinning a reproducible one.
- **P4 — author + capture the target. ✅ DONE (2026-06-01).**
  `tools/traces/new_game_to_map.jsonl`: 4-click nav → page-7 Start → `wait
  MAP_FREEROAM` → dismiss the five entry message-scrolls (hand-icon column
  spray) → `{capture}` the first full clean strategic-map frame
  (`ad450c26…`, bit-identical across two pinned runs). The new-game ground
  truth. **Open polish:** the visual message-scroll subsystem's active global
  (for `{wait_until scroll==0}` to name the exact first-clean frame instead of
  the round heuristic) is unidentified — the `0x426xxx` "scroll" family is the
  audio/wave timer subsystem (red herring).
- **P5 — port anchors.** `src/anchor_trace.{c,h}` wired into the main loop;
  drive the same trace through the drop-in. The anchor stream localises the
  divergence (the port stalls at the new-game transition) — the measured
  frontier. As `FUN_00497ced` + the map screen-arm land, the port anchors
  advance and its frame converges on the truth.
- **P6 (later) — `tas_diff.py` + scenario integration.** Anchor-aligned
  port↔retail diff gallery; fold into a `scenario-test.py` once the port
  renders past `TITLE`. A comparison run should pin `--rng-seed` by default.

## Relationship to existing roadmap

This is roadmap **Phase 5 (TAS bot)** + the verification backbone for the
strategic-map render frontier ([project_strategic_map_render_frontier]). It
does not change the porting order; it makes every render-parity check cheap,
repeatable, and grounded in a deterministic truth frame.
