# Main-menu render — chip plan

> 2026-05-29. Grounded in a live retail call-trace at the menu (cum-sec-gated
> window, `tools/call_trace_diff.py` machinery) + decompile xref. Goal: port
> the chips that make the port render the "Lords of the Realm 2 / The siege is
> on" main menu bit-identical to retail (`runs/groundtruth/mainmenu-retail.bmp`).

## How this was derived

Retail call-trace at the menu (424 hooked VAs, 8-frame window at cum-sec 1000)
showed `engine_tick` (FUN_004b99c0) dispatches ~51 fns/frame; the port scaffold
runs only the ~12-fn pre-tick block. The render phase is the gap. Walking
`ret_va` edges + decompile xref gives the exact menu render tree below.

## The menu render tree (confirmed)

```
engine_tick FUN_004b99c0 [tested]            per-tick driver (port: scaffold)
 └─ render phase  ← THE GAP (port has none)
     └─ FUN_0049a010 [ported,982B]           screen-state-0 dispatch  (confirm port wiring)
         └─ FUN_0043ac23/FUN_0043ad25 [unported,258/256B]   dispatch
             └─ FUN_0040f1a0 [unported,1117B]                pre-title dispatch
                 └─ FUN_0041e7e1 [unported,385B]   title-scene dispatch
                    │   gate: DAT_004eac50 == 0x1f (title screen-state)
                    │   page: DAT_005530f0 == 1 (main menu)
                    ├─ FUN_0041e61d [unported,452B]   photographic background blit
                    ├─ FUN_0041ea14 [unported,143B]   MAIN-MENU render root:
                    │   ├─ FUN_00409346 [unported,81B]  gray stone panel
                    │   │   ├─ FUN_0040946d [unported,856B]   panel fill leaf
                    │   │   └─ FUN_004097c5 [unported,367B]   panel frame leaf
                    │   ├─ FUN_00402c5e [tested] ×2     title + subtitle text  ✅ ported
                    │   └─ FUN_0041eaa3 [unported,487B] 4 buttons:
                    │       ├─ FUN_00403ee4 [tested]    button rect    ✅ ported
                    │       ├─ FUN_004527a6 [tested]    button border  ✅ ported
                    │       └─ FUN_00402c5e [tested]    button label   ✅ ported
                    └─ FUN_0045240a [tested,33B]        finalize       ✅ ported
```

The leaves rest almost entirely on **already-ported gfx primitives**
(`402c5e` text/gfx_wrap, `403ee4` rect/gfx_line, `4527a6` border). The button
renderer `FUN_0041eaa3` highlights the selected item via `DAT_00553114`.

## Chips to port (11 functions, ~5.3 KB)

| # | VA | size | role | rests on |
|---|----|----|------|----------|
| **Render leaves** ||||
| 1 | `FUN_0041ea14` | 143B | main-menu root (panel + 2 texts + buttons) | ported text + chips 2,5 |
| 2 | `FUN_00409346` | 81B | gray panel wrapper | chips 3,4 |
| 3 | `FUN_0040946d` | 856B | panel fill | (check leaves) |
| 4 | `FUN_004097c5` | 367B | panel frame | (check leaves) |
| 5 | `FUN_0041eaa3` | 487B | 4 buttons (rect+border+label, highlight selected) | ported primitives |
| 6 | `FUN_0041e61d` | 452B | photographic background blit | (check leaves) |
| **Dispatch (route engine_tick → title render)** ||||
| 7 | `FUN_0041e7e1` | 385B | title-scene dispatch (gates on screen-state 0x1f, page DAT_005530f0) | chips 1,6 |
| 8 | `FUN_0040f1a0` | 1117B | pre-title dispatch | chip 7 |
| 9 | `FUN_0043ac23` | 258B | dispatch | chip 8 |
| 10 | `FUN_0043ad25` | 256B | dispatch | chip 8 |
| 11 | engine_tick render phase | — | wire FUN_004b99c0's render block → FUN_0049a010 (ported) in `src/engine.c` | chip 9/10 |

## Data / state dependencies (the likely-bigger unknown)

The render reads state + assets that a *title-scene init* (separate from render)
must populate. These are a parallel track and may be the real long pole:

- `DAT_004eac50` = screen-state; must be **0x1f** at the title. Reaching it =
  the boot→title state transition the scaffold doesn't run.
- `DAT_005530f0` = menu page; **1** = main menu.
- `DAT_00553114` = selected button index (drives highlight).
- `DAT_004eabec` = panel/bg asset ptr — written by `FUN_004b25fc` / `FUN_004b25b0`
  (DDraw bring-up region; verify these run in the port).
- `&DAT_005b2fa0` (title strings) / `&DAT_005af8f0` (menu labels) — string tables;
  `localization.c` is partly ported (4 fns). Confirm these tables get loaded.

## Porting order (leaf-first, verify each step)

1. **Reach the title state** — get the port into `DAT_004eac50 == 0x1f`,
   `DAT_005530f0 == 1`. Find + port the boot→title transition (or, as a first
   bring-up shortcut, force these globals so the render path is exercisable
   before the full state machine lands). Confirm `FUN_0049a010` (ported) is
   actually called by the port's `engine_tick` render phase (chip 11).
2. **Background** — chip 6 (`41e61d`) + its asset load (`DAT_004eabec`). Verify
   pixels appear: `compare_frames` vs ground truth (expect bg-only match).
3. **Panel** — chips 2,3,4 (`409346`+`40946d`+`4097c5`).
4. **Title root + text** — chip 1 (`41ea14`); text uses ported `402c5e` + the
   string tables.
5. **Buttons** — chip 5 (`41eaa3`); ported primitives + label table + selection.
6. **Dispatch chain** — chips 7-10 to wire it faithfully (or keep the forced
   route from step 1 if exact dispatch parity isn't yet needed).

Verify each step:
```sh
# capture the port menu, diff vs retail ground truth
nix develop -c tools/run-openlords2.sh --frames 300 --gfx-init \
    --capture-to /tmp/port-frames --capture-every 100
nix develop -c python3 tools/compare_frames.py \
    --a /tmp/port-frames/frame_00200.bmp --a-label port \
    --b runs/groundtruth/mainmenu-retail.bmp --b-label retail \
    --out runs/compare/mainmenu          # iterate until identical:true
```

## Notes

- Each render chip is small and rests on ported primitives, so the *render*
  work is light. The **title-scene init + asset/string loading + the
  boot→title state transition** is the heavier, less-mapped track — scope that
  early (step 1) since it gates everything visible.
- `runs/groundtruth/mainmenu-retail.bmp` is the bit-exact target (gitignored).
- Re-derive this tree any time with the cum-sec-gated call-trace
  (`--call-trace-min-cum-secs 1000`) + the ret_va edge walk.
