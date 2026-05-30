# Title-screen menu sub-state machine + navigation

> 2026-05-29. The title screen (`DAT_004eac50 == 0x1f`) is itself a small
> state machine: a *page* selector `DAT_005530f0` chooses which menu the
> player sees. This documents the page map, the per-page draw/update
> dispatch, and the harness workflow for driving + capturing each page for
> render parity.

## Title page selector (`DAT_005530f0`)

Two parallel dispatch ladders run while `DAT_004eac50 == 0x1f`:

- **Draw** — `FUN_0041e7e1` (the deferred-screen dispatcher's 0x1f arm,
  reached via `FUN_0040f1a0`). Calls `FUN_0041e61d` (background) then a
  per-page draw fn.
- **Update** — the per-tick title handler at `0x4b99c0`+ (line ~88901).
  Per-page update fn.

| page (`DAT_005530f0`) | draw fn      | update fn     | screen |
|----------------------:|--------------|---------------|--------|
| 1   | `FUN_0041ea14` | `FUN_0041eaa3` | **Main menu** — Single player / Multiple players / Lords of Magic? / Exit game |
| 2   | `FUN_0041ec8a` | `FUN_0041ece6` | **"Your options"** (single-player) — Play Now! / Load a game / Skirmish! / Custom game / Back |
| 3   | `FUN_0041ef42` | `FUN_004149ec` | **"Loading a game." save/load list** — file grid + scrollbar (list render ported; button overlay pending) |
| 4   | `FUN_0041ef57` | `FUN_0041f321`+ | **"Choose your title and your shield"** — name box + 5 shields + Back/Continue (ported) |
| 5   | `FUN_0041f592` | `FUN_0041f5e8` | **"Play Now!" campaign chooser** — Original campaign / The new campaign (ported) |
| 6   | `FUN_0041f3e9` | `FUN_0041f4a1` | (sub-screen) |
| 7   | `FUN_0041f6c7` | `FUN_0041f98b`+ | (sub-screen) |
| 8   | `FUN_0041f77a` | `FUN_0041f98b`+ | (sub-screen) |
| 9   | `FUN_0041fdd6` | …            | **per-option detail dropdown** (over page 7/8) — render + input ported (values selectable) |
| 10  | `FUN_00420428` | …            | (sub-screen) |
| 0xb | `FUN_00420630` | …            | (sub-screen) |
| 0xc | `FUN_0042051c` | …            | save/load list (scrollbar — `FUN_00421005`) |
| 0xd | `FUN_0042150b` | …            | (sub-screen) |

> Note: the boot path (intro chain → 0x1f) lands on **page 1**, not page
> 0xc. The earlier assumption that the four-button menu was page 0xc was
> wrong — page 0xc is a list screen. Confirmed empirically: skip-intro
> lands on `screen=0x1f substate=0x1`, and clicking "Single player"
> transitions to `substate=0x2`.

## "Your options" (page 2) render — port target

`FUN_0041ec8a` (draw, 92 B):

```c
FUN_00409346(DAT_004eabec, 0xb0, 10, 0x12, 0x12);          // gray panel
FUN_00402c5e(0xb, 5, 0xb0, 0x2d, 0x120, &DAT_005b2fa0,0x3f);// "Your options" (large font, str 5)
FUN_0041ece6();                                            // the 5 buttons
```

`FUN_0041ece6` (buttons, 604 B): five buttons, each at x=0xe0 w=0xc0 h=0x18,
24 px apart (y = 0x5b, 0x7f, 0xa3, 0xc7, 0xeb):

```c
FUN_00403ee4(0xe0, y, 0xc0, 0x18);                         // button fill rect
FUN_004527a6(0xe0, y, 0xe, 3, 1);                          // bevel/border
FUN_00402c5e(0xb, str, 0xe0, y+5, 0xc0, &DAT_005af8f0, col);// label (menu font)
// col = 0xf9 (highlight) if DAT_00553114 == button_index, else 0x3f
```

Label string ids (localization entry 0xb, sub-string index): button 1 = 6
("Play Now!"), 2 = 7 ("Load a game"), 3 = 0x13 ("Skirmish!"), 4 = 8
("Custom game"), 5 = 9 ("Back."). Title = str 5 ("Your options").

All primitives already exist from the main-menu port (`gfx_panel`,
`gfx_text`, the bevel-rect + dirty-mark used by `engine_title_render_buttons`)
— page 2 is the same shapes with different geometry + strings. In the port
this lands as the `engine_title_render` page-2 renderer (today the deferred
`page_fn` stub).

## "Play Now!" campaign chooser (page 5) render — ported

Reached from "Your options" → **Play Now!** (page 2 button 1) when the
expansion pack (Siege Pack) is installed.  All strings come from
localization **entry 0x27** (not 0xb like pages 1/2):

- title  = sub 0 — "Expansion pack installed, choose:-"
- left   = sub 4 — "Original campaign"
- right  = sub 5 — "The new campaign"

`FUN_0041f592` (draw, 86 B):

```c
FUN_00409346(DAT_004eabec, 0x40, 0x32, 0x20, 8);            // gray panel
DAT_0058fe2c = 1;                                          // uppercase-highlight
FUN_00402c5e(0x27, 0, 0x40, 0x50, 0x200, &DAT_005b2fa0,0x3f);// title (large font)
DAT_0058fe2c = 0;
FUN_0041f5e8();                                            // the two buttons
```

`FUN_0041f5e8` (buttons, 223 B): two **side-by-side** buttons (unlike the
vertical main-menu/options stacks) — left at x=0x6e, right at x=0x16e, both
w=0xa4 h=0x18 at y=0x74; labels at y=0x7a w=0xa0 (menu font).  Highlight
0xf9 when `DAT_00553114` == 1 (left) / 2 (right), else 0x3f.  A **single**
dirty-box mark (`FUN_004527a6(0x70,0xd7,0xe,3,1)`) covers the whole row,
bracketed by the no-shadow mode (`DAT_005aea40`).

Background is the same gateway family as pages 1/2 (`FUN_0041e61d` maps
pages 1..6 → gateway.256/gateway.pl8), so no new asset path.  All draw
primitives are the ones already proven bit-identical on pages 1/2
(`gfx_panel_draw`, `gfx_wrap_localized_line_centered`,
`gfx_line_rect_bevel_palette_b`, the tiled dirty mark) — page 5 is the same
shapes with page-5 geometry + entry-0x27 strings.  Landed in
`engine_title_render.c` as `engine_title_render_play_now` /
`engine_title_render_play_now_buttons`; the scene dispatch routes page 5 to
it (and the per-frame refresh re-renders its buttons).  Driveable in the
drop-in via `--title-nav` (step 3 navigates page 1→2→Play Now→5).

**Page-5 input arm — ported.** `engine_menu_title_input` now handles page 5
(retail `FUN_004ba9c8` page-5 arm): the 2-entry hotspot table `DAT_004dccf8`
(left "Original campaign" box [0x70,0x76]–[0x110,0x8e] param 0 / right "The
new campaign" box [0x170,0x76]–[0x210,0x8e] param 1, both press-edge) drives
hover (→ `DAT_00553114`) and the click action `FUN_00433461`
(`menu_action_page5`).  The action records the campaign in the shared
save-schema scalars **`DAT_0053f640` = param** and **`DAT_0053f258` = 2 (new)
/ 0 (original)**, sets `DAT_0055302c = 1` (full-game substate), and advances
to **page 4** ("Choose your title and your shield").  The fade / camera-reset
/ `s011_02.wav` cue side-effects are deferred behind TODOs like the page-2
transitions.  Host-tested; page 4's *render* is still the next chip (today
the page-4 transition lands on the deferred page hook).

**Bit-identical to retail — verified (2026-05-29).** The drop-in page-5
render (`runs/title-nav-p5/nav_03_p5_playnow.bmp`, `--title-nav` step 3,
neutral) is **0 diff px** vs a fresh retail groundtruth
(`runs/groundtruth/playnow-retail.bmp`, captured by navigating page
1→2→Play Now→5 via `tools/traces/menu_play_now.jsonl`).  Both neutral (no
hover), 307200/307200 identical, `max_channel_delta=0`.

> 🔧 **Harness fix that unblocked this — input-replay viewport rescale.**
> The `--input-trace` menu navigation had stopped driving retail: clicks
> landed nowhere because the engine maps the WndProc-cached *client* cursor
> to game coords by `game = cached * game_extent / viewport_screen_extent`
> (FUN_004b191e), and under the capture's display path the viewport screen
> rect is the **desktop resolution** (e.g. `[0,0,1920,1080]`), not 640×480.
> A 640×480-authored trace coord therefore scaled *down* 3× (e.g. (320,107)
> → game (105,47)) and missed every hotspot — the page-transition action
> never fired.  Fix (`rescaleMouseLParamToClient` in the frida agent):
> pre-multiply replayed mouse coords by `viewport/game_extent` so the
> engine's own divide lands back on the authored game coord, resolution-
> independently (no-op when the viewport is already 640×480).  Traces stay
> authored in game space.  This restored both the page-2 and page-5 captures.
> (Diagnosed via a temp per-post dump of the cursor/extent/page globals.)

## "Choose your title and your shield" (page 4) render — ported

Reached from "Your options" → **Custom game** / **Skirmish!** (page-2
params 3 / 5, `FUN_00432cc8` → page 4, substate 2 / 3) or from the page-5
campaign chooser (`FUN_00433461`).  This is the player-setup screen: an
editable name/title box, a five-shield picker, and a Back/Continue row.

`FUN_0041ef57` (root, 200 B):

```c
DAT_0058fe2c = 1;                                          // uppercase highlight
FUN_00409346(asset, 0x50, 10, 0x1e, (gate ? 0x10 : 0xe)); // panel (taller in button layout)
FUN_00402c5e(0xb, 10, 0x50, 0x23, 0x1e0, &DAT_005b2fa0, 0x3f); // title (entry 0xb sub 10)
DAT_0058fe2c = 0;
DAT_005aea40 = 1;                                          // no-shadow ON
FUN_0041f321();                                            // name box
FUN_0041f1dd();                                            // 5 shields
DAT_005aea40 = 0;
if (gate) FUN_0041f01f();                                  // Back/Continue
```

where `gate = (DAT_00553030 == 0 || DAT_00553248 != 0)`.

- **`FUN_0041f321`** (name box): opaque box-frame sprite (panels2 index
  `0xcc`) via the new `FUN_0040a567`/`FUN_004b432e` opaque blit, then the
  name text (`DAT_0053f1e0`) via the editable-field renderer
  (`FUN_00402637`), then the blink caret (`FUN_0040acce`).  Caret-anchor
  bookkeeping: clear the first-pos latch, render with the editor cursor as
  capture target, fall back to the line-end X if not latched, then shift
  the caret to the box origin (`+0xd6`) and pin Y at `0x52`.
- **`FUN_0041f1dd`** (shields): 5 shields at `x = (i-1)*0x58 + 0x70`,
  `y = 0x8c` (sprites `i*2+0xcb` for empty / selected-flashing slots,
  `i*2+0xcc` otherwise).  The selected slot, while flashing and outside
  the blink-off window (`DAT_00568984 == 0`), also draws the highlight
  ring (sprite `0xd7`) at `(..+0x74, 0x70)`.
- **`FUN_0041f01f`** (buttons): Back (entry 0xb sub 9, select id **6**) at
  `(0x70, 0xd7)` and Continue (sub `0xb`, id **7**) at `(0x150, 0xd7)`,
  both `0xc0×0x18` bevel-`b` rects.  Continue dims to `0x20` when
  `DAT_0057c940 != 0`, else `0x3f`; highlight `0xf9` on hover.

Landed in `engine_title_render.c` as `engine_title_render_choose_title`
(+ the name-box/shields/buttons helpers); scene dispatch routes page 4 to
it.  State (gates, shield slots, selected/flash, name, editor) is bound
via setters and seeded for bring-up from a `--dump-globals` capture.

**Bit-identical to retail — verified (2026-05-29).** The drop-in page-4
render (`runs/title-nav-p4/nav_04_p4_choosetitle.bmp`, `--title-nav` step
4, neutral) is **0 diff px** vs a fresh retail groundtruth
(`runs/groundtruth/choose-title-retail.bmp`).  307200/307200 identical,
`max_channel_delta=0`.

> 🔧 **Quirk that cost the last 191 px — the steady-state name box is
> SHADOWED.**  `FUN_0041ef57` brackets the name box in no-shadow mode
> (`DAT_005aea40 = 1`), but the per-frame title-update ladder calls
> `FUN_0041f321` *standalone* every tick — outside that bracket, so with
> `DAT_005aea40 == 0` (shadow ON, `cursor_y` foreground at `param_3` plus
> shadow passes at `±1`).  A captured retail frame is the steady-state
> refresh, so its name text carries the drop-shadow the full-render pass
> omits.  Reproduced via `engine_title_render_refresh_choose_title`
> (routed through the page-4 refresh + called once after the full render
> in the bring-up).  Until that landed the glyphs sat 1 px low and the
> shadow row was missing — the entire 191-px residual.

> **State captured via `--dump-globals`** (run `gt-p4-state`): name
> `DAT_0053f1e0 = "aaaaaa    "`; shield slot 1 selected
> (`DAT_0057cb40[1]=1`, `DAT_0057c8cc=1`, flash `DAT_0057d354=1`, timer
> `DAT_00568984=0`); gates `DAT_00553030=0` / `DAT_00553248=0` (tall
> layout, buttons shown); Continue enabled (`DAT_0057c940=0`); overwrite
> editor at cursor 0; `DAT_005530f0=4`.

> **Full-glyph sweep — every menu-font glyph is bit-identical.**  Beyond
> the default "aaaaaa", the name box was swept across the whole printable
> set in ≤16-char batches: retail's name buffer (`DAT_0053f1e0`) forced
> with `frida_capture.py --poke-globals 0x53f1e0:<hex>` (writes the string
> every tick so it survives the engine's writes), the drop-in rendering
> the same via `--title-nav --title-name <str>`.  **0 glyph-shape diffs**
> across `A–Z`, `a–z`, `0–9`, and `.,!?:;-+=()&%`.  The only residual is
> the blinking caret (`FUN_0040acce`): its phase is non-deterministic, so
> a batch whose retail frame catches the caret ON shows ~10–34 px in the
> caret row (y 96–97) that the drop-in's off-phase frame lacks — masking
> that row gives 0 across every batch.  (Combined with the menu/options
> pages, which already exercise mixed-case + punctuation 0-diff, the menu
> font is fully validated.)

## "Loading a game." (page 3) save/load list — list render ported

Reached from "Your options" → **Load a game** (page-2 param 2,
`FUN_00432cc8` → page 3; scans `*.sav` via `FUN_004aecf0` into the entry
table).  The page-3 draw is `FUN_0041ef42` → **`FUN_004148e4(5)`** (frame)
→ **`FUN_004149ec(999, 1)`** (list content).

`FUN_004148e4` (frame): outer panel (`0x60,10,0x1c,0x15`) + clip-reset +
large-font title (entry **0x28** sub 5 = "Loading a game.") + the
action-button bevel rect (`0x70,0x42,400,0x100`) + three box outlines
(top selected-name field `0x78,0x4a,0xc0,0x20`; the list box
`0x78,0x72,0x160,0xa4`; a bottom box `0x78,0x11e,0x180,0x1c`).

`FUN_004149ec` (mode 1 — content): the selected-name field (tiled inset
via `FUN_004097c5`/`gfx_panel_fill_interior` + the name text `DAT_004ea130`
+ caret), then the **file grid** — entries at `&DAT_004e8790 + i*0x41`
(null-terminated name then ext) laid out **3 columns** (x step `0x78`)
× rows (y step `0x10`) starting at `(0x80, 0x76)`, walking
`DAT_004ea1a0`..`DAT_004eb25c`.  The selected entry (index == the call's
`param_1`; the draw pass passes **999** = none) gets a 96×16 highlight
bar (`FUN_004b414a`, glyph_w 6 / height_short 0x10) and text color `0x20`.
A bottom status line (`FUN_00402d37`) draws only when `DAT_0057d3c4 != 0`.

All primitives were already ported (`gfx_panel_*`, `gfx_line_*`,
`gfx_text_render_line`, the caret, `gfx_wrap_localized_line`) except the
selection-highlight fill `FUN_004b414a` (`gfx_sprite_fill_block`).
Landed in `engine_title_render.c` as `engine_title_render_filelist` +
`..._filelist_content`; scene dispatch routes page 3 to it; state bound
via setters and seeded from a `--dump-globals` capture.

**Page 3 fully bit-identical — 0 diff px (incl. the button overlay).**
The drop-in (`nav_05_p3_loadgame.bmp`, `--title-nav` step 5) now matches
retail (`runs/groundtruth/loadgame-retail.bmp`) **everywhere** — panel,
title, selected-name field, all four files, box outlines, AND the
formerly-missing 2870 px (all x ≥ 400): the **gauntlet action buttons +
scroll arrows**.  `compare_frames` → `identical: true`.

Those buttons are the per-frame button overlay
`FUN_0040cfd2(0x60,10,&DAT_004ddd78,4)` — the generic button-list
renderer, keyed off `FUN_004ba26e`'s 0x1f / page-3 arm.  Now ported:

- **`engine_button.c`** (`FUN_0040cfd2` → `engine_button_render_list`) —
  walks a `engine_button_rec_t` table (the 0x18-byte per-button struct,
  shared with the input hit-test), stages each button's 16-byte sprite
  descriptor and dispatches a fixed-size opaque raw blit.  Sheet select
  by `kind`: `< 0x18` → the mode-dependent misc sheet (`DAT_005530c8`,
  misc_*.pl8); `>= 0x18` → the static system sheet (`DAT_005bb540`,
  system2.pl8).  Frame select by pressed/hover state.  No clip (buttons
  never straddle the canvas edge).
- **`gfx_sprite_blit_24x24_raw`** (`FUN_004b4cb8`) — the one new blit,
  the 24×24 middle sibling of the 16×16/32×32 raw blits (kinds 0x11 /
  0x18; same hardcoded 640-byte dst stride, quirk #75).
- **`gfx_load_system_sheet`** loads system2.pl8 (verified byte-identical
  to the live `DAT_005bb540` at the page-3 frame), bound via
  `engine_title_render_bind_system_asset`.

The **page-3 button table** `DAT_004ddd78` (r2 dump @ 0x4ddd78, 4
records, all edge-type 4, all reading system2.pl8): the two gauntlet
action buttons (Load / Cancel) at sprites 0x1d / 0x1f, kind 0x20 (32×32);
the scroll up / down arrows at sprites 0x23 / 0x25, kind 0x18 (24×24).
Lives as `k_filelist_buttons` in `engine_title_render.c`; the page-3 arm
of `engine_title_render_refresh_buttons` (and `--title-nav` step 5, post
full-scene) paints it via `engine_title_render_filelist_buttons`.

> **Which system sheet?** Both `system2.pl8` (index 0) and `system.pl8`
> (index 1) load into the same `DAT_005bb540` buffer; the active one is
> `DAT_004dbbf0`.  Live `--dump-globals` at the page-3 frame: index **0**
> → system2.pl8.  The on-disk file (56518 B) equals the live blob's
> leading bytes exactly, and the idx 0x1d/0x1f/0x23/0x25 descriptors
> match (32×32 / 24×24), so a raw disk read reproduces it bit-for-bit.

The same overlay subsystem — shared by every screen — now also unblocks
the gauntlet buttons + scrollbars on pages 6..0xd as they land (each
just needs its `DAT_004dd***` button table + the right sheet bound).

> **State captured via `--dump-globals`** (run `gt-p3-state`): 4 entries
> in scan order (lastturn / old_turn / safeturn / test, 0x41-byte
> records, name + "sav"); top field `DAT_004ea130` = "lastturn";
> first-visible 0; count 4; `DAT_0057d3c4 = 0` (no bottom status);
> `DAT_005530f0 = 3`.

## Page 9 — per-option detail dropdown (`FUN_0041fdd6`) — ported

Clicking an option tab on the page-7 setup screen opens **page 9**, a small
chrome dropdown listing that option's selectable values.  The render
(`FUN_0041fdd6`, 415 B) is two parts:

1. **Backdrop redraw.**  It re-renders the page it came from in full — the
   bookmark `DAT_00553e5c` routes to `FUN_0041f6c7(tab)` (page 7),
   `FUN_0041f77a(tab)` (page 8 MP), `FUN_0042051c` (0xc) or `FUN_00420630`
   (0xb).  Passing the edited `tab` suppresses *that* tab's value cap in the
   strip, since the dropdown covers it.  Only the page-7 backdrop is ported;
   the other three are deferred with their pages.

2. **The value list.**  A chrome bevel (`FUN_004093e0`, mode-1 bevel) anchored
   from `DAT_004d3158[tab]` `{x, y}`, sized `count + 2` rows, then `count`
   centered text rows (entry `0x67` sub `DAT_004d3128[tab] + i`).  Row `i` is
   highlighted red `0xf9` when `i+1 == DAT_00553114` (the hover/selection
   index, shared with the menu-button highlight), else `0x3f`.  A
   `FUN_004527a6` box mark closes it.

```c
// FUN_0041fdd6, condensed
if (bookmark == 7) FUN_0041f6c7(tab);              // backdrop, edited tab hidden
x = DAT_004d3158[tab].x;  y = DAT_004d3158[tab].y;
if (tab == 2) y += (count-1) * -0x10;              // tab 2 grows UP (engine-quirks #121)
rows = count + 2;
FUN_004093e0(x, y, 6, rows);                       // chrome bevel
for (i = 0; i < count; i++)
    FUN_00402c5e(0x67, DAT_004d3128[tab]+i, x+1, i*0x10+y+0x10, 0x60, font,
                 (i+1 == DAT_00553114) ? 0xf9 : 0x3f);
FUN_004527a6(x, y, 6, rows, 1);
```

**State** is published by the tab click `FUN_00433998` (port:
`menu_action_p7_tab`): `DAT_0055306c = tab` (clicked param − 1),
`DAT_00553fb4 = DAT_004d5560[tab]` (the value count), with tab 2 clamped to
`roster_count − 1` (`DAT_0053e8a4`) when the roster is < 5 seats, and the
return bookmark `DAT_00553e5c`.  The dropdown's own scroll origin
(`_DAT_00553530 = 0`) is **deferred** (none of the 12 options has more rows
than fit, so it never scrolls).  The page-9 **input arm is ported** — see
"Page 9 input" below; the values list is now selectable.

Per-tab value tables read verbatim from the binary (`r2 pxw`):

| field | symbol | values (tabs 0..11) |
|-------|--------|---------------------|
| value count | `DAT_004d5560` | 2 2 4 2 4 4 6 4 5 3 7 2 |
| `0x67` sub base | `DAT_004d3128` | 0 2 5 9 0xb 0xf 0x13 0x19 0x1d 0x22 0x25 0x2c |

Port: `engine_title_render_option_detail` + `engine_title_render_set_option_detail`
in `src/engine_title_render.c`; `menu_action_p7_tab` in `src/engine_menu_input.c`.
Bring-up render: `--title-nav` step 6 (`nav_06_p9_optiondetail.bmp`) — the
"Advanced Farming" tab (0) dropdown shows **off**/**on** with the current
value (off) highlighted.  See engine-quirks #121 for the full-scene-redraw,
grow-upward, and dead-table-column oddities.

### Page 9 input — selectable values (`FUN_004ba9c8` page-9 arm) — ported

The page-9 arm of the dispatcher (`FUN_004ba9c8`, `DAT_005530f0 == 9`) makes
the dropdown values pickable.  Per frame, in order:

1. **Hover** (`FUN_00433ce4`) — walk the `DAT_00553fb4` value rows of the
   open tab's panel; the row under the game-space cursor
   (`DAT_004e65fc`/`DAT_004e65f4`) becomes the highlight `DAT_00553114`, or 0.
   The row boxes are exactly the dropdown's rendered text rows: column
   `[x, x+0x60)`, each 16px tall at `panel_y + i·16 + 16`, with tab 2 shifted
   up by `(count-1)·16` (the grow-upward quirk #121).
2. **Pick** (`FUN_00433a23`) — on the LMB press edge (`DAT_004eafb4`) with a
   live highlight, `FUN_00433ba2(tab, DAT_00553114-1)` writes the value back
   to that option's scalar (one of the 12 permuted `DAT_0053f288..0053f2b4`),
   then the dropdown closes: `DAT_005530f0 = DAT_00553e5c` (the bookmark),
   `DAT_0057d340 = 2`.
3. **Re-target** — else the page-7 option-tab grid (`DAT_004dce30`, 0xc) is
   dispatched; clicking *another* tab re-opens the dropdown on it
   (`FUN_00433998` skips the re-bookmark since the page is already 9).
4. **Dismiss** — else any remaining press edge closes the dropdown back to
   the bookmark.  (A press on the country list or buttons just dismisses;
   page 9 doesn't dispatch them.)

Because the tab's hotspot box overlaps its own dropdown value rows, the
hover→pick check (step 1–2) runs **before** the tab dispatch (step 3), so a
click on an overlapping value row picks the value rather than re-opening the
tab.

Deferred with the MP/roster subsystem: `FUN_00433ba2`'s recompute tail
(`0x433cc1` → `DAT_0053f268`, then the `0x49caaa` AI-seat/preview rebuild) and
the MP pick sound (`0x43eda0(9,0)`, gated on `DAT_00553030`).  The value cap
updates immediately; the roster does not yet re-seat on a player-count change.

Port: `engine_title_render_option_detail_hover` / `_pick` / `_bookmark` /
`_tab_value` in `src/engine_title_render.c`; `menu_action_p9_pick` + the
page-9 arm in `src/engine_menu_input.c`.  Bring-up: `--title-nav` step 7
(`nav_07_p7_optionpicked.bmp`) drives the real dispatch — clicks tab 0, picks
row 2 ("on"), and captures the page-7 setup screen with "Advanced Farming"
flipped **off → on**.  5 host tests (hover/pick/re-target/dismiss + tab-2
grow-up).

## Input side — hotspot hit-test + page navigation (`engine_menu_input`)

> 2026-05-29. The *render* half above is matched by the *input* half: the
> per-page hotspot hit-test that resolves hover + clicks into the
> selected-button highlight and page transitions. Ported in
> `src/engine_menu_input.{c,h}` (pages 1 & 2); host-tested; driveable in
> the drop-in via `--title-nav`.

The retail input chain, per frame, while `DAT_004eac50 == 0x1f`:

```
FUN_004b99c0 (per-tick driver)
  → FUN_0042ff10 (game state machine)        [0x42ff10, NOT ported — outer]
      → FUN_004ba9c8 (UI input dispatcher)    [0x1f arm ported]
          → FUN_0040e3ee  click hit-test + dispatch
          → FUN_0040e5e7  hover hit-test → DAT_00553114
          → FUN_00432b05  page-1 click action  (the transitions)
          → FUN_00432cc8  page-2 click action
```

**Hotspot table entry** (24 B, retail `DAT_004dcb48` page 1 / `DAT_004dcba8`
page 2; dumped via r2):

| off  | field        | meaning                                       |
|-----:|--------------|-----------------------------------------------|
| 0x00 | short x0,y0  | box top-left                                  |
| 0x04 | short x1,y1  | box bottom-right (half-open: `x0<=cx<x1`)     |
| 0x08 | code*        | click action (reads `DAT_0059154c` = +0x10)   |
| 0x0d | byte         | per-entry "clicked" latch (unused by menu)    |
| 0x0f | byte         | button-edge type: **1**=press, **3**=release, **2**=held/repeat |
| 0x10 | int          | action param → `DAT_0059154c`                 |
| 0x14 | int          | second param → `DAT_00591550` (0 for menu)    |

**Page-1 hotspots** (all at x∈[0xe0,0x1a0); rows step 0x24): Single player
(type 1, param 1), Multiple players (1, 2), Lords of Magic? (**type 3**,
param 4), Exit game (1, 3). **Page-2 hotspots** (one extra row to 0xeb):
Play Now! (1, 1), Load a game (1, 2), Skirmish! (1, 5), Custom game (1, 3),
Back. (1, 4).

**Click-edge gate** (`FUN_0040e3ee`): type 1 fires on `DAT_004eafb4`
(LMB press edge), type 3 on `DAT_004e65d8` (release edge), type 2 on
press/dblclk or the `DAT_0057d3c8` 320 ms repeat tick while held. The
edge flags + game-coord cursor (`DAT_004e65fc/f4`) are exactly what the
already-ported input resolver (`FUN_004b191e` / `engine_compute_input_state`)
emits each frame.

**Navigation transitions** (`DAT_005530f0` = page):

| page | button         | →                                  |
|-----:|----------------|------------------------------------|
| 1    | Single player  | page 2 (or deferred page 10 if `DAT_005c9a74` ∉ [1,2]) |
| 1    | Exit game      | quit latch `DAT_005aeb68 = 1`      |
| 2    | Back.          | page 1                             |
| 2    | Play Now!      | page 5                             |
| 2    | Load a game    | file-list page 3                   |
| 2    | Custom / Skirmish | page 4 (substate `DAT_0055302c` 2 / 3) |
| 5    | Original / New campaign | page 4 (`DAT_0053f640`=0/1, `DAT_0053f258`=0/2, `DAT_0055302c`=1) |

Each transition also sets the demand-redraw cookie `DAT_0057d340 = 2`. The
click actions' *other* side-effects — MP bring-up (0x4978ad/0x4b7250),
save-list enumeration (0x4aecf0), promo movie (0x42d91b), profile scan
(0x49bae9), sound/fade cues — are deferred behind TODOs; the page
transitions are faithful.

### Wiring + bring-up

`engine_tick` runs `engine_tick_menu_input_step` right after the input
resolver, gated on `g_sim_current_screen == 0x1f` — so once a producer
sets the title screen live, the menu navigates on its own. Until then the
scripted `--title-nav DIR` bring-up drives the real dispatch with
synthetic clicks (cursor parked on a button + LMB press edge) and captures
each resulting page: verified main menu → "Single player" → Your options →
"Back." → main menu, every frame **bit-identical** to the page groundtruths
(0 diff px), round-trip clean.

**Live interactive menu — DONE (`--title-live`, 2026-05-29).** The three
pieces that were deferred all landed:

- **(a) canvas-locked demand-redraw seam.** `engine_tick_title_render_step`
  (engine.c) wraps `engine_screen_dispatch` in one `gfx_lock_canvas`.  The
  page background reload (own DD lock) runs *before* the canvas lock — the
  drop-in's `gfx_title_background_load` can't nest inside it — bound via the
  new `engine_bind_title_bg_reload_fn` hook (→ `gfx_title_background_load`).
- **(b) per-frame button refresh** = `engine_title_render_refresh_buttons`,
  the port of `FUN_004ba26e`'s 0x1f arm.  Retail calls `FUN_004ba26e`
  unconditionally every frame (engine_tick / FUN_004b99c0 line 99); its 0x1f
  arm re-renders just the current page's buttons (page 1 → `buttons`, page 2
  → `options_buttons`).  Runs each frame inside its own short canvas lock so
  the selected-button highlight (`DAT_00553114`, set by the input dispatcher
  with no demand-redraw) repaints without a full-scene redraw.  Pages 1, 2,
  3, 4, 5 and **7** now have their refreshers ported (page 7 = the dirty-
  gated country-list + option-tabs refresh, `engine_title_render_refresh_setup`;
  see PROGRESS 2026-05-30 + engine-quirks #119); pages 6, 8..0xd still don't,
  so the live menu round-trips 1..5 and 7 today.
- **(c) startup producer** = `engine_set_current_screen(0x1f)` +
  `engine_title_render_set_page(1)` + `engine_set_viewport_game_extents(640,
  480)` (so `engine_compute_input_state` maps the WM_MOUSEMOVE-cached cursor
  1:1 on the menu) + an initial `engine_set_demand_redraw(2)`.  `--title-live`
  binds the render state + bg hook and drives these, then the normal main
  loop ticks the engine — input resolver → menu hover/click → locked title
  render step.  Stands in for the unported boot→title transition (0x42b780).

Verified: page-1 menu renders **bit-identical** to `mainmenu-retail.bmp`
(0 diff px) and stays identical across 120 frames of per-frame refresh;
`--title-nav` still bit-identical on all three pages.  Human-confirmed a real
mouse navigates (Single player → Your options → Back).

**Hover highlight — bright red, and faithful (corrected 2026-05-29).** The
selected button's label re-renders every frame in palette index **0xf9**, and
on any full-color desktop retail renders that as **bright red** `(252,0,0)`.
We *briefly* believed there was no visible highlight and that the blackout was
faithful — but that was an artifact of our own `--force-windowed` hook (it
forces `DAT_0053f228 = 0`, putting retail on the 8bpp `FUN_0042f449` palette
builder, which blacks the reserved range).  **Real retail on a full-color
desktop runs fullscreen-exclusive → `FUN_0042f306`, which loads idx 1..254
from the `.256`, so the highlight (and the dark-red title caps at idx 1) show.**

Confirmed empirically (2026-05-29): a live fullscreen-exclusive retail capture
(`--no-force-windowed --no-gdi-seat`) read idx 0xf9 = `(252,0,0)` and the red
hover was visible on screen; the drop-in now defaults to the `FUN_0042f306`
build (`gfx_set_palette_full_range(1)`) and its windowed `--title-nav` "Single
player"-hovered main menu is **bit-identical (0 diff px)** to the real
fullscreen retail frame with the same button hovered (445 bright-red highlight
px + 362 dark-red caps px in both).  See engine-quirks #115.  `--title-nav` /
`--title-live` therefore now show the red hover highlight.

## Harness workflow: drive + capture a menu page

1. **Reach the menu fast** with `--skip-intro` (see
   [`main-menu-parity.md`](main-menu-parity.md)).
2. **Navigate** with a synthetic input trace (`--input-trace`). Cursor
   coords are client/game 640×480 (1:1); `lParam = (y<<16)|x`; events are
   `engine_tick`-stamped. Non-turbo gives predictable ~60 Hz ticks, so a
   click at tick 70 lands ~1.2 s in (after the menu is up at ~tick 10).
   Reusable trace: `tools/traces/menu_single_player.jsonl`.
3. **Capture + compare** the resulting frame against the page's groundtruth.

Verified 2026-05-29: skip-intro → click (320,107) "Single player" →
`screen=0x1f substate 0x1→0x2`; captured "Your options" frame is stable +
matches the real screen. Cursor read-back confirmed our posted
`WM_MOUSEMOVE` reaches the engine (`game_x=320`, cache disabled).

### `_SmackWait` robustness (skip-intro)

The first skip-intro cut depended on `_SmackWait` (smackw32 Ordinal_32, IAT
`0x5cf5d8`) eventually reporting "ready" (return 0) so the frame-snap could
reach `FUN_0042dbc7`'s done→chain branch. Under **silent-audio + non-turbo**
the decode thread never reported ready, leaving the engine stuck at
`screen=0x22` (movie state) with the menu drawn but **non-interactive**
(menu input is gated on `screen==0x1f`). Fix: the agent now also forces
`_SmackWait → 0` during a step-time boot-intro snap (same gate: skip on,
engine state 1, not inside open). Skip-intro now reaches `0x1f` reliably in
turbo *and* non-turbo. This was the prerequisite for deterministic-tick menu
navigation.

## Misc sprite sheet (`DAT_005530c8`) — mode-dependent, bound for the setup flow

The setup-flow pages (6..0xd) and their "kind < 0x18" gauntlet buttons read
the **misc** sprite sheet `DAT_005530c8`, a single buffer retail fills from
different files per game mode:

| loader | file | mode |
|--------|------|------|
| `FUN_00499034` | `misc_sel.pl8` (160540 B) | **setup / selection** (new-game flow; pages 6..0xd) |
| `FUN_00498dcb` | `misc_ske.PL8` (166336 B) | skirmish editor |
| (sibling loaders) | `Misc_bat.pl8` / `Misc_cty.pl8` | battle / city HUD |

`FUN_00499034` mallocs `DAT_004daee8` = `0x28488` (165000 B, r2-confirmed)
and reads the file whole from offset 0 via `FUN_004af8d9` — so the on-disk
`misc_sel.pl8` equals the live `DAT_005530c8` blob byte-for-byte (same as
system2.pl8).  Ported as `gfx_load_misc_sheet` (`gfx.c`), bound via
`engine_title_render_bind_misc_asset`; `main.c`'s title bring-up loads it
alongside system2.pl8.

Descriptor decode (16-byte records at `blob + idx*0x10 + 8`; `w@0`,
`height_short@2`, `pixoff@4`, `h@0xd`) of the setup-flow indices:

| idx | w × h | use |
|----:|-------|-----|
| 0xc / 0xd / 0xe | 64×64 | column-header icons (pixoff steps 0x1000 = 64×64) |
| 0xf | 320×240 | page-7 backdrop (`FUN_0040a21a(sheet,0xf,0xa0,0)`) |
| 0x10 | 130×130 | (page-7) |

Indices ≥ 0x36 (the in-game HUD's `0x36/0x39/0x3b`…) decode to garbage in
misc_sel — they live in the battle/city variants, not the selection sheet.
