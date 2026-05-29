# Main-menu render — chip plan

> 2026-05-29. Grounded in a live retail call-trace at the menu (cum-sec-gated
> window, `tools/call_trace_diff.py` machinery) + decompile xref. Goal: port
> the chips that make the port render the "Lords of the Realm 2 / The siege is
> on" main menu bit-identical to retail (`runs/groundtruth/mainmenu-retail.bmp`).

## How this was derived

Retail call-trace at the menu (424 hooked VAs, 8-frame window at cum-sec 1000)
showed `engine_tick` (FUN_004b99c0) dispatches ~51 fns/frame; the port scaffold
runs only the ~12-fn pre-tick block. The render phase is the gap. The tree below
is **re-derived from the static decompile** of `FUN_004b99c0` /
`FUN_0040f1a0` / `FUN_0041e7e1` / `FUN_0041ea14` (see `docs/decompiled/by-address/`),
which corrects two attribution errors in the original call-trace derivation —
see "Correction (2026-05-29)" at the bottom.

## The menu render tree (corrected, decompile-verified)

```
engine_tick FUN_004b99c0 [scaffold]          per-tick driver
 └─ render phase  ← THE GAP (port has only the input + tail steps)
     └─ @line 36:  if (DAT_0057d340 != 0) { FUN_0040f1a0(DAT_0057d340); DAT_0057d340 = 0; }
        │  DAT_0057d340 = DEMAND-REDRAW request (mode 1|2), set by 50+ input/
        │  state handlers (incl. the menu state tick) whenever the screen must
        │  repaint.  This — NOT FUN_0049a010/FUN_0043ad25 — is the menu render seam.
        └─ FUN_0040f1a0 [unported,1117B]   screen-dispatch (big switch on BYTE DAT_004eac50)
            │   the redraw-mode arg is forwarded as param_1; the 0x1f arm ignores it
            └─ DAT_004eac50 == 0x1f → FUN_0041e7e1 [unported,385B]   title-scene dispatch
               │   page: DAT_005530f0 == 1 (main menu)
               ├─ FUN_0041e61d [unported,452B]   background + ASSET LOAD (long pole):
               │     FUN_004af502 (chdir/IO), FUN_004b1897, FUN_004b11ce,
               │     FUN_004af8d9 (PL8 loader → gateway_256/panels2.pl8 into
               │     DAT_004ea8a0 / DAT_004eabec), FUN_004b25b0
               ├─ FUN_0041ea14 [unported,143B]   MAIN-MENU render root:
               │   ├─ FUN_00409346(DAT_004eabec,…) [unported,81B]  gray stone panel
               │   │   ├─ FUN_0040946d [unported,856B]   panel fill  → tile-blits
               │   │   │     via FUN_004b446d / FUN_004b44d2 / FUN_004b43b1 (long pole)
               │   │   └─ FUN_004097c5 [unported,367B]   panel frame → FUN_004b49f7 (long pole)
               │   ├─ FUN_00402c5e [tested] ×2     title + subtitle text  ✅ ported
               │   └─ FUN_0041eaa3 [unported,487B] 4 buttons:
               │       ├─ FUN_00403ee4 [tested]   bevel rect    → gfx_line_rect_bevel_palette_b ✅
               │       ├─ FUN_004527a6 [tested]   dirty-mark    → engine_dirty_rect_mark_box_tiled ✅
               │       └─ FUN_00402c5e [tested]   button label  → gfx_wrap_localized_line_centered ✅
               └─ FUN_0045240a [tested,33B]        finalize      ✅ ported
                  (also sets DAT_005651ac=1, DAT_004eb260=1)
```

Two distinct tracks fall out of this:

- **Render composition (light)** — `FUN_0041eaa3` (buttons) and the text leaves
  rest entirely on already-ported primitives.  Caveat: the call site passes
  `FUN_00402c5e(loc_idx, skip, x, y, w, &table, color)` (7 args) but the port
  `gfx_wrap_localized_line_centered` takes 10 — the menu's `font_base` /
  `char_glyph_table` / `metrics` are folded globals at the call site and must be
  resolved before the port is faithful, so these are **moderate**, not mechanical.
  `FUN_004527a6` is a dirty-rect *mark*, not a visible border (plan formerly
  mislabeled it).
- **Panel + background + state (the real long pole)** — see below.

## Chips to port

| # | VA | size | role | rests on |
|---|----|----|------|----------|
| **Render composition (moderate; ported primitives + folded font globals)** ||||
| 1 | `FUN_0041eaa3` | 487B | 4 buttons (bevel-rect + dirty-mark + label, highlight `DAT_00553114`) | ported primitives + menu font globals |
| 2 | `FUN_0041ea14` | 143B | main-menu root (panel + 2 texts + buttons) | chip 1 + ported text + panel chips |
| 3 | `FUN_0041e7e1` | 385B | title-scene dispatch (gate `DAT_004eac50==0x1f`, page `DAT_005530f0`) | chips 2,6 |
| 4 | `FUN_0040f1a0` | 1117B | screen-dispatch switch (only the 0x1f arm needed for the menu) | chip 3 |
| 5 | engine_tick render phase | — | wire the `if (DAT_0057d340) FUN_0040f1a0(DAT_0057d340)` seam into `src/engine.c` | chip 4 |
| **Long pole — panel/background tile-blit + asset load (DDraw track)** ||||
| 6 | `FUN_0041e61d` | 452B | background + PL8 asset load into `DAT_004eabec` / `DAT_004ea8a0` | asset loaders below |
| 7 | `FUN_00409346` | 81B | gray panel wrapper | chips 8,9 |
| 8 | `FUN_0040946d` | 856B | panel fill (tile loop) | tile-blit family |
| 9 | `FUN_004097c5` | 367B | panel frame (tile loop) | tile-blit family |
| — | `FUN_004b446d` / `44d2` / `43b1` / `49f7` | — | PL8 tile blitters (DDraw) | gfx surface lock/blit |
| — | `FUN_004af8d9` (+ `004af502`, `004b1897`, `004b11ce`, `004b25b0`) | — | PL8 loader + IO/chdir + scratch-buffer alloc | io.c / gfx.c |

## Data / state dependencies (the likely-bigger unknown)

The render reads state + assets that a *title-scene init* (separate from render)
must populate. These are a parallel track and may be the real long pole:

- `DAT_004eac50` = screen-state; must be **0x1f** at the title. Reaching it =
  the boot→title state transition the scaffold doesn't run.
- `DAT_005530f0` = menu page; **1** = main menu.
- `DAT_00553114` = selected button index (drives highlight).
- `DAT_004eabec` = panel/bg asset ptr — written by `FUN_004b25fc` / `FUN_004b25b0`
  (DDraw bring-up region; verify these run in the port).
- `&DAT_005b2fa0` / `&DAT_005af8f0` are **fonts, not string tables** (the prior
  cut had this wrong).  `&DAT_005af8f0` is the engine's default font — already
  named `g_default_font` in `src/gfx_dispatch.c` (and `g_debug_label_font` in
  `engine_battlefield_render.c`); `&DAT_005b2fa0` is the larger **title** font.
  The actual menu **strings** come from localization-blob entry `0xb` of
  `DAT_00591580` (loaded by config: `FUN_004af8d9(s_l2_eng, &DAT_00591580, …)`,
  see `config.c:215`), addressed by sub-string index:
  | call | skip (sub-string) | font_base |
  |------|------|------|
  | title    | 0    | `&DAT_005b2fa0` |
  | subtitle | 1    | `&DAT_005af8f0` |
  | button 1 | 2    | `&DAT_005af8f0` |
  | button 2 | 3    | `&DAT_005af8f0` |
  | button 3 | 0x2f | `&DAT_005af8f0` |
  | button 4 | 4    | `&DAT_005af8f0` |
- **Full arg resolution for the text leaves** (so `gfx_wrap_localized_line_centered`
  can be called faithfully — the call site's 7 args expand to the port's 10):
  retail `FUN_00402c5e(0xb, skip, x, y, w, &FONT, color)` →
  `gfx_wrap_localized_line_centered(loc=DAT_00591580, loc_index=0xb,
  skip_strings=skip, cursor_x=x, cursor_y=y, width=w, font_base=&FONT,
  char_glyph_table=&DAT_004d71f0, char_glyph_table_metrics=&DAT_004d71d0,
  color=color)`.  The two glyph maps are the same fixed table at 0x20 offset
  (renderer subtracts 0x20 before indexing `&DAT_004d71f0`, so
  `&DAT_004d71f0[c-0x20] == &DAT_004d71d0[c]`); both are static binary data,
  not loaded assets.  Verified via `FUN_004025d7`/`FUN_00402637`/`FUN_004014f0`.
  Note `FUN_00402637` special-cases `DAT_004eac50 == 0x1f`/`0x1c` (title screens)
  with a taller line box (0x36 vs 0x10) — the title font is rendered taller.
- `DAT_00553114` = selected button index (1..4) — drives the highlight: the
  selected button's label color is `0xf9` instead of the default `0x3f`.

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

## Step-1 RE findings (title-scene state machine, 2026-05-29)

The "reach the title state" track is a real state machine, not just a flag:

- **Screen-state transition**: `FUN_0042b780` (119B) sets `DAT_004eac50 = 0x1f`
  (title), but to page `DAT_005530f0 = 0xc` (SP) / `0xb` (MP) — NOT the main
  menu (page 1). It also runs title init: `FUN_0042b7f7` (asset/state setup,
  pulls in `FUN_00499a1c` music + `FUN_0046ead0`/`FUN_00498dcb`), `FUN_0042c5fe`,
  `FUN_0042bf46` (loads many title globals: `DAT_0051fad0`, `DAT_00521c90`, …).
- **Main-menu page (`DAT_005530f0 = 1`)** is set by the menu state-tick, not the
  transition. Key callers that BOTH call `FUN_0042b780` AND set page 1:
  `FUN_0042ff10` and `FUN_00497a34`. `FUN_0042ff10` is in engine_tick's direct
  menu callees (fires every menu frame) → it's the **title/menu state tick**
  (input → page/selection → transitions). Its sibling input/selection state:
  `DAT_00553114` (highlighted button).
- So the menu needs a **logic chip** (`FUN_0042ff10` state tick, + the
  transition `FUN_0042b780` + its init callees) feeding the **render chip**
  (`FUN_0041e7e1` tree). Render reads what the tick writes.

**Implication for porting order:** the render leaves are cheap, but they draw
nothing without (a) the title init having loaded the bg/panel/string assets and
(b) the state tick having set `DAT_004eac50=0x1f`/`DAT_005530f0=1`. Bring-up
shortcut still recommended: force those globals + manually invoke the asset
loaders once, port + verify the render leaves, THEN port the state tick +
transition for faithful behaviour.

## Notes

- Each render chip is small and rests on ported primitives, so the *render*
  work is light. The **title-scene init + asset/string loading + the
  boot→title state transition** is the heavier, less-mapped track — scope that
  early (step 1) since it gates everything visible.
- `runs/groundtruth/mainmenu-retail.bmp` is the bit-exact target (gitignored).
- Re-derive this tree any time with the cum-sec-gated call-trace
  (`--call-trace-min-cum-secs 1000`) + the ret_va edge walk.

## Correction (2026-05-29) — what the original call-trace derivation got wrong

The first cut of this plan (commits cc3b13d / e9f2165) walked `ret_va` edges from a
live menu trace and produced a render tree rooted at
`FUN_0049a010 → FUN_0043ac23/FUN_0043ad25 → FUN_0040f1a0`.  Reading the static
decompile shows two errors:

1. **`FUN_0049a010` is not a render dispatch and is not ported.**  It is the
   in-game **state-machine driver** (sub-step body, `engine_tick` line 261),
   a switch on the game-phase global `DAT_00569584`.  Its `else` (in-game) arm is
   the strategic-turn driver that calls the just-ported `FUN_0049b762`
   (turn-end check) — which is why `engine_turn.h` correctly calls 0x49a010
   "the unported strategic-turn driver".  It draws nothing.

2. **`FUN_0043ac23`/`FUN_0043ad25` do call `FUN_0040f1a0`, but render screen 0,
   not the title.**  `FUN_0043ad25` executes `DAT_004eac50 = 0;` immediately
   before `FUN_0040f1a0(local_8)`, so its dispatch hits the `== 0` arm
   (`FUN_0040f5fd`).  These are turn/screen-**teardown** handlers, not the menu
   render path.

The real menu render is the **demand-redraw seam** in `engine_tick`'s render
phase (line 36): `if (DAT_0057d340 != 0) FUN_0040f1a0(DAT_0057d340)`, evaluated
every frame while `DAT_004eac50 == 0x1f`.  `DAT_0057d340` is set to 1 or 2 by the
input/state handlers (50+ write sites) when the menu needs repainting.  The
forwarded value is a redraw-mode hint that the 0x1f title arm ignores.

Net effect on the chip list: the two "dispatch" chips `FUN_0043ac23`/`FUN_0043ad25`
are **dropped** (not on the menu path); the wiring chip becomes "add the
`DAT_0057d340` demand-redraw call to `FUN_0040f1a0`", not "route through
`FUN_0049a010`".
