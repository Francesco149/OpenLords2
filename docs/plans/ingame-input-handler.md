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

## Risks / notes

- Edge-scroll reads the live host cursor (FUN_004b191e = GetCursorPos) vs screen
  metrics — in headless capture the cursor sits at a screen edge, which is exactly
  why retail scrolls in the capture. Porting it makes the port match WITHOUT the
  suppress hack, but the port must read the same cursor source as retail.
- Many sub-handlers share button-table data in .rdata (e.g. 0x4dc428, 0x4dc680) —
  extract those tables as the handlers land.
- The shared tail `FUN_004323fe` (right-panel tab strip) fires across most screens;
  port it once with the skeleton.
