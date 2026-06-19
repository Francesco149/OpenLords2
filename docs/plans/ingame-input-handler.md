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

Only the **dismiss chip** is wired: `engine_popup_scroll_input_step` (FUN_0047685d)
is called from the tick (engine.c ~2152), gated to `g_engine_state == 3`. Nothing
else of FUN_0042ff10 exists — clicks on the map / HUD / End-Turn do nothing.

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
