# In-game input handler — port plan

> 2026-06-19. User-directed: port the strategic in-game input dispatcher so the
> real click paths work (End Turn → season reports, tile right-click →
> terrain-info, county/army clicks). This is the enabler that makes the
> remaining event-popup arms ([[project_event_popup_arms]]) naturally
> triggerable + verifiable. Grounded in the static decompile of
> `FUN_0042ff10` + its sub-handlers (`docs/decompiled/by-address/`).

## What it is

`FUN_0042ff10` @ 0x42ff10 (8140 B) is the per-tick in-game input/UI dispatcher
(retail tick FUN_004b99c0 calls it before the render phase). It is a **screen
dispatch** keyed on `DAT_004eac50` = the current-SCREEN byte (g_sim_current_screen
mirror; see engine-quirk #197). Shape:

```
FUN_0042ff10:
  DAT_0052afa0 = DAT_0057cac4; DAT_0057cac4 = 0; ...    prologue latches
  FUN_00432640()                                        per-frame input setup (262 B)
  if (FUN_0047685d() == 0 && FUN_004ba9c8() == 0) {     popup-dismiss chip [PORTED] + menu pump
    if (input not swallowed) {
      switch (DAT_004eac50) {                            ← THE DISPATCH
        case 0x00:  strategic map      ← PRIORITY (lines 33-65)
        case 0x02/0x0f/0x05/0x06/0x04:  army-move / battle sub-modes
        case 0x08..0x1d, 0x1f, 0x20:  county/market/other sub-screens (transition table)
        case 0x28..0x45:  more sub-screens
      }
      ... shared tail (FUN_004323fe right-panel tabs) ...
    }
  }
LAB_00431f25: edge-latch bookkeeping; DAT_004eabb4 = 0;
```

Most case bodies are SCREEN-TRANSITION tables (set DAT_004eac50 = next screen +
DAT_0057d340 = demand-redraw on a hit). The strategic case (0x00) is the one that
fans out to the real map-input handlers.

## Current port state

The LIVE tick (engine.c ~2152, gated `g_engine_state == 3`) still calls only the
**dismiss chip** `engine_popup_scroll_input_step` (FUN_0047685d) directly — the
dispatcher is NOT wired in yet (see the wiring recipe below).

But the testable SPINE of the dispatcher + the terrain-info data path is now
ported as pure/unit-tested modules (2026-06-20 session):

| piece | retail | port | status |
|---|---|---|---|
| cursor → map cell + `map_ready` | FUN_00429ba4 | `engine_camera_cursor_to_cell` (engine_camera.c) | DONE, 14 tests |
| dispatcher skeleton (screens 0 + 4) | FUN_0042ff10 | `engine_ingame_input_dispatch` + `_classify_region` (engine_ingame_input.c) | DONE, 27 tests |
| terrain-info compute (tile → attrs) | FUN_0046d5fe | `engine_map_query_terrain_info` (engine_map_query.c) | DONE, 8 tests |

The dispatcher is a pure function: per-frame input snapshot + mutable state struct
+ a HOOKS struct of sub-handler/action callbacks (NULL = inert). It ports the
prologue latches, region classify, dismiss-chip subsumption, the full strategic
(==0) screen incl. the map fall-through (dismiss / view-mode-2 / army-LMB /
terrain-RMB), the matched terrain-info (==4) close case, and the shared tail.

## 2026-06-20 (session 2) — terrain-info LANDED end-to-end; render VISUALLY VERIFIED

All three pieces ported + committed; the panel render is confirmed correct via a
forced-render seat.

| piece | retail | port | status |
|---|---|---|---|
| opener glue | FUN_0043caf4 | `engine_terrain_info_open` (engine_terrain_info.c) | DONE, 10 tests (commit 14a6810) |
| panel render | FUN_0041befe + FUN_0041c208 | `engine_terrain_info_render` + the two classifiers (engine_terrain_info.c) | DONE, 9 tests (commit ad236b2) |
| live wiring | FUN_0042ff10 tick seam | `engine_tick_ingame_input_step` + screen-4 arm (engine.c) | DONE, builds clean (commit 9574bce) |

- The **parchment box is the already-ported `gfx_panel_draw`** with the panels.pl8
  chrome sheet (quirk #207); the text uses the same localized-wrap primitives the
  event-scroll renderer plumbs; the terrain icon loads from icon_tmp.pl8.  The army
  sub-renderers (FUN_0041cb3a/d299/da2f), the complex county/army panel
  (FUN_0041b19d), and the FUN_00426216 SFX cue (quirk #208) are DEFERRED — guarded
  where retail branches, so common terrain renders + the army arms are stubbed.
- **VISUAL VERIFICATION (the win):** a forced-render seat `--ti-cell <cell>` runs the
  terrain compute for a descriptor cell + paints the screen-4 panel over the
  `--strat-map` map (bypasses the blocked live input path).  Cell 2080 →
  a polished, correct panel: "Kent" (county, blackletter title), "Scrubland."
  (terrain name), the landscape icon (sprite 0x17), and the wrapped body "Empty
  land that may be crossed, but is unusable for farming." on the parchment chrome.
  The render is byte-plausible vs retail (structure + strings + icon all correct);
  the pixel-exact zoom-diff is the remaining polish pass.
- **LIVE path blocked on a SEPARATE frontier:** the live RMB→terrain-info path is
  wired but cannot be triggered in a both-engines capture because the port never
  reaches a clean in-game map — a re-posting "Game Objectives" / report-screen
  popup (the unported 0x27 scroll renderer, [[project_loadflip_rng_clarified]])
  sits on the map, so the dispatcher consumes the RMB as a dismiss (correct
  behavior).  No regression: the welcome-series deterministic seal-dismiss still
  works.  Verifying the live RMB end-to-end needs that popup frontier closed (or an
  interactive seat on a hand-cleared map).

## What's left for terrain-info ("Here Be Dragons", map RMB) — DONE (see above)

Three pieces, all best done WITH the in-game viewer (render-parity + integration):

1. **Opener orchestration — port FUN_0043caf4** (354 B, decompile read). It is glue
   over already-ported pieces: re-resolve hover (FUN_00429ba4 param 1) → copy tile
   (`DAT_0056795c = DAT_00568974`) → cell col/row (`DAT_0053f064 = tile/DAT_0057d338`,
   `_DAT_0053e98c = %DAT_0052afd4`, `_DAT_0053e99c = /DAT_0052afd4`) → `FUN_0046d5fe`
   (= engine_map_query_terrain_info, DONE) → set `DAT_00553e68 = county` if nonzero,
   dismiss any open scroll (FUN_00476768) → `_DAT_005681cc = 3` → army-hover branch
   (select army via FUN_0043278b + map render FUN_004050c0) → render (FUN_0041b032)
   + FUN_004b37bc. Model as a function-with-hooks like the dispatcher; the col/row
   math + state flags + army-hover branch are unit-testable.
2. **Panel render — port FUN_0041befe** (778 B) — the screen-4 simple-terrain panel
   (the `DAT_0052afbc==0 && DAT_0052afb4==0` arm of FUN_0041b032). Paints the
   parchment chrome + "Here Be Dragons" title + the terrain-name string. RENDER-
   PARITY work: run the zoom truth|mine|diff loop, push to the feed, r2-verify.
   (The complex arm FUN_0041b19d, 3425 B, is the county/army info panel — defer.)
   The screen-4 render arm wiring: engine_screen_dispatch currently routes screen 4
   → other_stub; add a `screen==4 → FUN_0041b032` arm (cf. the FUN_0040f1a0 screen-4
   case = `FUN_0041b032()`).
3. **Live wiring — replace the bare dismiss call in engine.c's tick** (~2152) with:
   - build `engine_ingame_input_in_t` from the live globals (the input mirrors
     g_input_*, g_sim_extended_pause, g_sim_turn_progress/_timeout_reached,
     s_slot_0057cb18 view_mode, the game-mode DAT_0057c8fc; region_in from the
     cursor + HUD panel rect);
   - `map_ready` + the hovered tile from `engine_camera_cursor_to_cell` fed the live
     camera geometry (expose it from engine_map_render.c via a getter — the geometry
     lives in its statics g_cam_*);  retail computes this in the render tick
     (FUN_004b99c0 line 63: `DAT_005533b0 = FUN_00429ba4(0)`), so call the resolver
     once per frame before dispatch and mirror DAT_00568974/DAT_005533b0;
   - bind hooks: `dismiss_chip = engine_popup_scroll_input_step`,
     `terrain_info_open` = the FUN_0043caf4 port, `terrain_info_close` →
     FUN_0045270a (demand-redraw; FUN_0043cc56 just calls it), `army_select` =
     FUN_0043ce1a (defer → NULL until ported), the HUD sub-handlers stay NULL;
   - keep the reactive auto-dismiss poke (set rmb_released before dispatch).
   **VERIFY:** capture `tools/traces/terrain_info.jsonl` → the port must now show the
   terrain-info panel on the tile RMB matching retail; no-regression: the welcome
   series stays byte-exact (the dismiss path is subsumed unchanged).
   ⚠ Do NOT flip `map_ready` live until the screen-4 renderer (piece 2) exists, or
   a tile RMB flips to a renderer-less screen 4 (visible freeze).

### Deferred-but-located (the OTHER strategic paths)
- **End-Turn → reports (plan step 2):** the HUD button row hit-test FUN_00432967 +
  FUN_0040e3ee (the latter is the analog of the already-ported
  engine_menu_hotspot_click — a 0x18-byte button-table walker). Then the turn
  advance + the season production report 0x10 render arm.
- The remaining strategic sub-handlers (edge-scroll FUN_00432221, FUN_00439079,
  FUN_0043292d, FUN_00438ceb, FUN_00439122, FUN_00438e3b) + the ~30 non-strategic
  screen cases each land with their screen/feature; the dispatcher already has the
  NULL hook slots + (other-screen) deferral comments.

## Strategic-screen (DAT_004eac50 == 0) sub-handler chain (the priority target)

Called in order; each returns nonzero ⇒ input consumed ⇒ goto tail. Sizes:

| handler | B | role |
|---|---|---|
| `FUN_00432221` | 477 | **edge-scroll** (→ FUN_00431f59 712B). KNOWN gap — the port lacks it, captures use `--suppress-edge-scroll` (see project_trace_studio_v3 camera-gap). |
| `FUN_00439079` | 169 | map drag / scroll-by-drag |
| `FUN_00432967` | 61 | HUD button hit-test @ (0x1de,0x1ae) via FUN_0040e3ee — the **End-Turn / right-panel** button row |
| `FUN_0040deca` | 152 | (short table @ 0x4dc428, n=3) keyboard/edge table |
| `FUN_0043292d` | 58 | HUD button hit-test |
| `FUN_00438ceb` | 326 | map-tile / army click |
| `FUN_00439122` | 537 | map-tile click (select / move / **right-click → info**) |
| `FUN_00438e3b` | 574 | right-HUD-panel region clicks (mouse x≥0x1de): minimap / production toggles (sets DAT_00553f54) |

Key shared callees: `FUN_0040e3ee` (505 B, button hit-test+action), `FUN_0040e7e4`
(87 B, generic click-consumed test), `FUN_00432640` (262 B, per-frame input prep).

Mouse state globals (already mirrored in the port via main.c WndProc): `DAT_004e65fc`
= cursor x, `DAT_004e65f4` = cursor y, `DAT_004e6900` = RMB-newly-released,
`DAT_004e65d8` = LMB edge. `DAT_0057d340` = demand-redraw (engine_set_demand_redraw).

## Incremental plan (each step verifiable before the next)

1. **Dispatcher skeleton + edge-scroll.** Port the FUN_0042ff10 prologue + the
   screen switch with ONLY the strategic (0x00) case, calling stubs that return 0
   for every sub-handler EXCEPT `FUN_00432221`/`FUN_00431f59` (edge-scroll), which
   ports fully. **Verify:** capture grid_load_dense WITHOUT `--suppress-edge-scroll`
   — the port must now scroll col_start like retail (the artifact `--suppress-edge-scroll`
   was added to neutralize → confirms parity). No-regression: welcome series still
   byte-exact. Removes a known port/retail divergence + proves the skeleton.
2. **End-Turn → turn advance.** Port `FUN_00432967` + `FUN_0040e3ee` (the End-Turn
   button) and whatever action it fires. **Verify:** a trace that clears the welcome
   series (deterministic dismiss) then clicks End-Turn → the turn advances; capture
   the season-boundary production report (popup 0x10) → diff vs retail (this also
   lands the 0x10 render arm).
3. **Tile right-click → terrain-info.** Port `FUN_00439122` (tile click) + the
   terrain-info popup opener + its renderer (identify: FUN_0047309e arm vs separate).
   **Verify:** `tools/traces/terrain_info.jsonl` (already built) — port must show
   "Here Be Dragons / <terrain>" matching retail.
4. County/army click, market, etc. — as the remaining cases are needed.

## Verification vehicles (ready)

- `tools/traces/terrain_info.jsonl` — clears welcome (reactive), right-clicks a
  tile. Today: retail shows terrain-info, port shows clean map (gap). Step 3 closes it.
- `tools/traces/turn_probe.jsonl` — deterministic-ish welcome walk; extend with an
  End-Turn click for step 2.
- olv3 `--suppress-edge-scroll` toggle for step 1.
- DETERMINISTIC input only for port↔retail diffs — reactive `{auto_dismiss}` desyncs
  the engines ([[project_event_popup_arms]]).

## Step-1 RE findings (2026-06-19, scoping pass — read the full chain)

Read the whole dispatcher + every strategic sub-handler + the edge-scroll tree.
Key facts that refine the plan:

- **The input SAMPLER is already ported.** `FUN_004b191e` (981 B, GetCursorPos +
  button-edge detection + 300 ms delayed-click) → the port's `engine_tick_input_step`
  already produces the cursor/button/edge mirrors (DAT_004e65fc/f4 client cursor,
  DAT_004e6900 RMB-released, DAT_004e65d8 LMB-released, DAT_004eafb4 LMB-pressed,
  DAT_004e6594/98 screen cursor). The dispatcher only CONSUMES these — no sampler
  port needed.
- **The 8 strategic dispatch-chain handlers are ALL HUD-region (x ≥ 0x1de) or
  edge-scroll** — none handle a map-area click. (439079 = minimap rclick; 432967/
  43292d = button hit-tests via FUN_0040e3ee @ 0x262/0x1de; 438ceb = the 4-button
  action grid x[0x1e7,0x276]; 439122 = right-panel scrollbar; 438e3b = right-panel
  production toggles; 0040deca = a 3-region table @ 0x4dc428; 432640 = a cursor-
  region classifier into _DAT_005534f0, NOT a click handler.)
- **The MAP-tile interaction is the strategic FALL-THROUGH**, after every HUD
  handler returns 0 (dispatcher lines 43-61), gated on `DAT_005533b0` (cursor-over-
  map):
    - LMB released (DAT_004e65d8) → `FUN_0043ce1a()` — army select / move.
    - **RMB released (DAT_004e6900) → `DAT_004eac50 = 4; FUN_0043caf4()`** — this is
      the **terrain-info** open ("Here Be Dragons"): screen 4 + FUN_0043caf4. The
      screen-4 popup is the tooltip-sized/position-tracking arm (popup type 4,
      geometry 0xb0×0x30, FUN_0047309e case 4 — still an UNPORTED popup arm).
  So **terrain-info (step 3) = dispatcher fall-through + `FUN_0043caf4` opener +
  the type-4 / screen-4 renderer + the `DAT_005533b0` map-cursor gate.** Moderately
  deep but now fully located.
- **Edge-scroll (former step-1 pick) is a POOR first increment**: FUN_00431f59
  adjusts col_start (DAT_005651b4) / vertical pixel (DAT_005651b8) / alt-view
  (DAT_005678a0/0055ce70) by DAT_0055cd48, validates via FUN_004bbbe3, marks dirty
  via FUN_0045270a — AND it reads the live GetCursorPos screen cursor, so porting it
  RE-INTRODUCES the camera-gap flake (see project_trace_studio_v3); captures would
  need port-side `--suppress-edge-scroll`. Verify its LOGIC by unit test (deterministic),
  not capture. Defer unless gameplay edge-scroll is explicitly wanted.

**Revised first-increment recommendation:** the dispatcher SKELETON (faithful
prologue latches + FUN_00432640 region-classify + dismiss-chip subsumption +
strategic-screen shell with the fall-through LMB/RMB-over-map branches + tail),
then port the RMB→terrain-info path (`FUN_0043caf4` + type-4 arm) for a verifiable
win via `terrain_info.jsonl`. End-Turn → reports stays the deeper step.

## Risks / notes

- Edge-scroll reads the live host cursor (FUN_004b191e = GetCursorPos) vs screen
  metrics — in headless capture the cursor sits at a screen edge, which is exactly
  why retail scrolls in the capture. Porting it makes the port match WITHOUT the
  suppress hack, but the port must read the same cursor source as retail.
- Many sub-handlers share button-table data in .rdata (e.g. 0x4dc428, 0x4dc680) —
  extract those tables as the handlers land.
- The shared tail `FUN_004323fe` (right-panel tab strip) fires across most screens;
  port it once with the skeleton.

## 2026-06-20 (session 3) — End-Turn LANDED; turn-advance season change VERIFIED; AI-brain is the last gate

The strategic in-game frontier moved a long way this session.

**Every-frame map composite (commit d7cc930, engine-quirk #209).** The root
cause of the "0x27 report renderer" gap was NOT a screen-0x27 dispatch arm
(FUN_0040f1a0 has no 0x27 case) — it was that the port only composited the map
on the demand-redraw seam, never retail's EVERY-FRAME map scene (FUN_004050c0,
tick line 42).  Ported it (full strat render each in-game frame, anim counters
free-running via a LIVE/PINNED flag so the seats stay byte-exact).  Result: the
boot 256-warning behind the welcome scroll is GONE (endturn_seal t030
14.82%→0.08%, user-confirmed "popups 1:1"); grid_load_dense parity HELD at
0.055%.  This is also the render substrate for everything dynamic — animation,
army moves, AND the season change below.

**End-Turn button (commit ef7a675).**  `hud_btn_a` hook = FUN_00432967: the
End-Turn button is entry 5 of table DAT_004dc680 (rect [0,0x1e]-[0xa1,0x31] at
origin 0x1de,0x1ae, type-1 LMB-press, action FUN_0043ac23), hit-tested via the
reused engine_menu_hotspot_click (= FUN_0040e3ee).  The action (SP arm of
FUN_0043ac23) sets the active realm's ready slot to 999.  The 5 panel TABS in
that row flip to unported sub-screens → deferred.

**Season bank reload (commit ef7a675).**  After the turn passes rotate the
season, engine_turn_advance reloads the strategic banks (engine_pl8 keys the
table off the season) so the new season's terrain shows — retail does this
lazily on the screen-0x22 rollover-scroll close (unported); the port reloads
eagerly.

**VERIFIED end-to-end (tools/traces/endturn_advance.jsonl, port-only):**
click End-Turn on a clean map → turn rolls over → season 4(Winter)→1(Spring):
terrain snow→green AND HUD "Winter"→"Spring", composited live.  Pushed to feed.

**⚠ THE LAST GATE — AI-brain completion (next frontier).**  The turn only
advances with `OL2_FORCE_AI_READY` (parks the non-current AI realms ready=999).
Without it the per-tick AI brain FSM (FUN_0049a581 / engine_army_ai_run_per_tick)
STALLS: slot 84 (brain cursor) parks at 4, slot 85 (players-still-thinking)
stays 5, and the AI realms never set their own ready=999 → engine_turn_end_check
never resolves phase 4.  Retail's AI completes (its turn processes in seconds);
the port's brain parks short.  So a FULLY-NATURAL turn advance (no dev flag) is
gated on the AI brain reaching its turn-end — the deeper sim frontier to chase
next.  Everything downstream of "all realms ready" (resolve → phases 5/6/7 →
rollover → season change → reload → render) is proven working.

**Capture caveat (CRITICAL, engine-quirk territory):** WSL→Windows env vars do
NOT cross unless listed in WSLENV.  flake.nix now forwards OL2_NO_HINTS +
OL2_FORCE_AI_READY (alongside OPENLORDS2_GAME_DIR); set the values AND rely on
that WSLENV entry, or getenv() in the .exe silently sees nothing.  Also: a
trace's injected cursor reaches the game via the WM_MOUSEMOVE cache (1:1 at the
pinned 640×480 client) — position-INDEPENDENT input (scroll dismiss) worked
before without this, but the End-Turn HIT-TEST is the first capture-verified
position-DEPENDENT click.

## 2026-06-30 (session 4) — the "AI-brain completion stall" was a mis-diagnosis; natural turn advance PROVEN, no force-ready needed

Investigated session 3's "LAST GATE" as the next task.  It was not a sim bug.

**Root cause of the session-3 finding:** `engine_army_ai_run_per_tick`
(FUN_0049a581) advances a realm's brain-state ladder, then — once the ladder
is done — waits for `engine_army_state_arm_movement_owned` to report no
pending owned movement before parking the realm at 999.  That function only
RE-ARMS the army records' substate for the movement executor; it never drains
them itself.  The actual drain is `engine_army_state_run` (FUN_004650b0) +
its movement executor (`engine_army_move_run`, FUN_00465d28) — a SEPARATE
system, called right after the brain dispatcher in engine.c's live sub-step
trio.  Session 3's finding drove the brain dispatcher without that
cooperating system (and/or read a capture window too short to cover the
ladder), so it looked like a permanent stall when it wasn't one.

**Proved convergence two ways:**
1. **Host** — new regression test
   `tests/test_game_init.c:test_game_init_ai_brain_converges_full_turn`:
   builds a real 5-player (1 human + 4 AI) new-game world, drives
   `engine_army_ai_run_per_tick` + `engine_army_state_run` together (matching
   the live tick), simulates the human's own End-Turn (ready=999). All 4 AI
   realms reach ready=999 in **~224 sub-step iterations** — bounded, no
   force-ready. (An isolated first run — brain dispatcher alone — reproduced
   the ORIGINAL symptom exactly: all 4 realms parked at state=801, each
   blocked on one never-draining army record. That run confirmed the
   diagnosis before the fix-shaped test was written.)
2. **Live**, on the real compiled port — `tools/traces/endturn_advance_long.jsonl`
   (the proven `endturn_advance.jsonl` prefix — load lastturn.sav, dismiss the
   boot 256-colour warning, click End-Turn — with a much longer capture
   tail), run via `--turbo --input-segtrace ... OL2_NO_HINTS=1` and
   **no OL2_FORCE_AI_READY**, ~3300 frames.  A new "sim-state" exit log in
   main.c (turn/phase/season/ai_cursor/not_ready/ready[]) showed a genuine
   **turn 1→2 rollover**: `ready=[1,1000,1000,1000,1000]` — all 4 AI realms
   self-parked; only the human (no 2nd End-Turn click sent) still pending.

**OL2_FORCE_AI_READY is a capture speed-up, not a workaround** — its doc
comment in engine.c (and 3 other stale "sub_steps is always 0" comments left
over from before the in-game state machine landed) corrected accordingly.

**Known gap, not fixed (low priority):** `--newgame-live` and
`--input-segtrace` are mutually exclusive in main.c's bringup dispatch
(combining them silently runs 1 frame and exits rc=0, no error — the
trace-consumption code sees a never-loaded trace as "exhausted"). The live
proof above therefore used the LOAD route (`lastturn.sav`) rather than a
truly fresh new-game; the fresh-game path is host-proven only. Fix the
dispatch exclusivity (or add an explicit error when a trace never loads) if
a `--newgame-live` + `--input-segtrace` combination is needed later.

**This closes the "LAST GATE for a NATURAL turn advance" framing.** Natural
turn advance (no dev flags except OL2_NO_HINTS, which dodges the unrelated
welcome-scroll sim-freeze) works end to end: human End-Turn click + AI
brain/movement convergence + season rollover.
