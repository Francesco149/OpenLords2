# Strategic-mode overlay renderer — units + cities pass

The OVERLAY pass renders units (armies) and cities/towns/settlements
on top of the strategic-mode territory layer.  It is the **second**
of two back-to-back full-map sweeps the parent caller invokes each
frame:

  - `FUN_00405602` (this cluster's driver)  → overlay pass
  - `FUN_00405487` (primary pass; ported)   → territories layer

Same 2-D tile array, same per-tile descriptor table, same camera —
but different cursor offsets, different descriptor flag bits tested,
and different per-tile drawers.  See `map-render.md` for the primary
pass and the underlying storage layouts.

## Cluster

| addr        | role                                          | size  |
|-------------|-----------------------------------------------|------:|
| `0x405602`  | outer frame driver                            | 608 B |
| `0x405eb5`  | phase-0 row iterator                          | 247 B |
| `0x405fac`  | phase-1 row iterator                          | 497 B |
| `0x4081a6`  | per-tile drawer — cities (bit 0x40)           | 658 B |
| `0x408c50`  | debug-overlay text helper                     | 390 B |
| `0x4071a0`  | per-tile drawer — units / armies (bit 0x80)   |3179 B |
| `0x407f82`  | army-label sub-renderer (called by army drawer)| 548 B |

All seven functions are now ported.  The army drawer (the largest)
houses the unit-category animation state machine plus four secondary
dispatch paths keyed by descriptor flag bits.  The army-label sub-
renderer (`FUN_00407f82`) was a child call surfaced during the army
port; it draws a fixed "label background" sprite (0x82) plus a
centered decimal text under it.

## Dispatch model

Each non-empty cell visited by an iterator runs through:

    flag = descriptors[cell + 2];
    if (flag & 0x80) army_drawer(phase);   // ← bit 0x80 → armies
    if (flag & 0x40) city_drawer(phase);   // ← bit 0x40 → cities

Both bits can be set on the same cell; both drawers fire.  Bit `0x20`
(used by the primary pass for fog-of-war gating) is NOT consulted.

The city drawer CLEARS bit `0x40` after dispatching (`& 0xbf`).  See
quirk #68.

## Cursor model

The overlay pass uses the SAME cursor variables as the primary pass —
`gfx_glyph`'s `g_cursor_x` and `g_cursor_y` (retail `DAT_00591524` and
`DAT_00591528`).  But because the two passes run sequentially (not
concurrently), each pass reinitializes the cursor at the start of its
frame.  No interleaving conflict.

The city drawer additionally maintains a per-record cursor save/restore
pair (anchor X/Y are preserved across the city sprite's clip-+-dispatch
sequence).  Identical pattern to the primary pass's per-record save
inside `engine_map_render_tile`.

## Driver shape (`FUN_00405602`)

    if (view_mode == 1) {
        for (y = 0x18; y < 0x1bc; y++)
            paint_4px_horizontal(0, y, 0);    // pre-clear left strip (quirk #69)
    }

    cursor_y = y0;
    cursor_x = x0;
    cur_row  = row_start;
    col_y_accum = 0;
    cur_col  = col_start;

    // Top phantom row — col_count cells, phase 0, NO debug text
    for (c = 0; c < col_count; c++) {
        cell = map[cur_row][cur_col++];
        if (cell < EMPTY) dispatch_cell(0);
        cursor_x += x_step;
    }
    cursor_y += y_step;
    cur_row += 1;

    phase1_row();
    for (r = 0; r < (row_count - 2) / 2; r++) {
        phase0_row();
        phase1_row();
    }

    // Bottom phantom row — mirror of top, on the cur_row left over
    cursor_x = x0;
    cur_col  = col_start;
    for (c = 0; c < col_count; c++) {
        cell = map[cur_row][cur_col++];
        if (cell < EMPTY) dispatch_cell(0);
        cursor_x += x_step;
    }

## Row iterators

### Phase-0 row (`FUN_00405eb5`)

    cursor_x = x0;
    cur_col  = col_start;
    for (c = 0; c < col_count; c++) {
        cell = map[cur_row][cur_col++];
        if (cell < EMPTY) {
            dispatch_cell(0);
            debug_text();              // ← fires on every non-empty cell
        }
        cursor_x += x_step;
    }
    cursor_y    += y_step;
    cur_row     += 1;
    col_y_accum += y_step;

No left-edge / right-edge cells (quirk #65) — strict difference from
the primary pass's phase-0 row.

### Phase-1 row (`FUN_00405fac`)

    cursor_x = x0;
    cell = map[cur_row][col_start];
    if (cell < EMPTY) dispatch_cell(1);  // phase 1 first cell
    cursor_x += x_half_step;             // unconditional half-step

    for (c = 0; c < col_count - 1; c++) {
        cell = map[cur_row][cur_col++];
        if (cell < EMPTY) {
            dispatch_cell(0);
            debug_text();                // ← fires on middle cells only (quirk #66)
        }
        cursor_x += x_step;
    }

    cell = map[cur_row][cur_col++];
    if (cell < EMPTY) dispatch_cell(2);  // phase 2 last cell (no debug text)

    cursor_y    += y_step;
    cur_row     += 1;
    col_y_accum += y_step;

Visits `col_count + 1` cells total (one phase-1, `col_count - 1`
phase-0, one phase-2).  The first and last cells skip the debug-text
helper — see quirk #66.

## City drawer (`FUN_004081a6`)

Per-tile drawer for descriptor flag bit `0x40`.  Renders a city /
town / settlement sprite from the RLE overlay blob (retail
`DAT_0055ce5c` — same blob the primary pass's record-type 1/2 overlay
path uses).

### Sprite-index derivation

    raw       = descriptors[cell + 1];
    first     = descriptors[cell + 0];
    large_sel = raw & 0x50;                      // bits 0x40 | 0x10
    if ((raw & 0x80) && first != 0x14) large_sel = 1;

    size_val = per_cell_size_table[cell / 8] - 1;
    if (territory[+0x154] - territory[+0x153] < size_val) size_val = 0;

    sprite_idx = large_sel ? 0x4e : (size_val + 0x38);

The "large select" overrides the size-based path: it picks the fixed
"castle" sprite at index `0x4e`.  Three independent triggers select
the large variant:

  - `raw & 0x10` set (any reason)
  - `raw & 0x40` set
  - `raw & 0x80` set AND first descriptor byte != 0x14

The third trigger's first-byte gate is interesting: it suggests the
0x14 value is a sentinel meaning "small even when large-flag is set" —
likely a settlement type that has its own visual representation.

### Per-territory cap

The per-cell-size value is capped by the ACTIVE territory's max-size
band at `territory[+0x153] / +0x154`.  The cap is asymmetric for
negative values (quirk #67) — values < 0 pass through unchecked.

### Sprite dispatch

    cursor_save = cursor;
    cursor.x   += phase == 1 ? (0x14 - x_half_step) : 0x14;
    cursor.y   += 6;
    dirty_rect_mark_tile_32(cursor.x >> 4, cursor.y >> 4);
    clip_x(x0, 0x1de);
    clip_y(0x18, view_mode == 1 ? 0x1d8 : 0x1da);
    dispatch_sprite_rle(overlay_blob);
    cursor = cursor_save;

### Validation gate

`src_off < 0` or `src_off >= 0x4baf1` (i.e. > 310000) bumps the
shared error counter `_DAT_0058fea8` and skips the dispatch.  The
threshold matches the primary pass's `> 310000`.

## Army drawer (`FUN_004071a0`)

Per-tile drawer for descriptor flag bit `0x80`.  3179 B in retail —
the largest function in this cluster by an order of magnitude.
Combines:

  - a 4-way **flag-bit priority chain** on `descriptor[+1]` (bits
    0x40, 0x10, 0x20, then 0x80-only) selecting one of four
    sub-dispatchers,
  - a 13-category **state-machine** under the 0x80-only path that
    animates unit frames by writing to `descriptor[+3]`, and
  - a shared dispatch tail that stages from the overlay blob,
    validates `src_off < 0x11171` (= 70001 — TIGHTER than the city
    drawer's 310001, see quirk #71), applies the clip, and
    dispatches via the RLE sprite family.

### Top-level filter

The drawer is gated by a dev-mode filter at the very top:

    if (render_mode == 1 && (descriptor[cell + 2] & 0x20) == 0) return;

`render_mode` is `DAT_0053f264` (also used by the primary pass).
When set to 1 (development mode), only cells with bit 0x20 of the
DISPATCH flag (the primary pass's "fog-of-war" bit) draw armies.
Production-mode (`render_mode == 0`) skips this filter — armies
draw on every cell that reached the army drawer at all.

Then the high-nibble gate:

    if ((descriptor[cell + 1] & 0xf0) == 0) return;

### Flag-bit priority chain on `descriptor[+1]`

In retail's nested if/else order — strict priority, not bitwise OR:

| `raw_flag` bit set | next condition          | path                       |
|--------------------|-------------------------|----------------------------|
| `0x40`             | `descriptor[+4] & 0xf` ∈ {0, 2} → fortress / march-out | see below |
| `0x40` (other low-nibble) | (none — fall through)  | **stale sprite** (quirk #70) |
| `0x10` (no 0x40)   | `descriptor[+0] == 0x13` | animation-end (tick+0x28)  |
| `0x20` (no 0x40, 0x10) | `descriptor[+0] ∈ [0xf, 0x16]` | directional arrows  |
| only `0x80`        | `descriptor[+0] < 13`    | unit-category state machine |
| only `0x80`        | `descriptor[+0] ∈ [13, 0x19] \ {0x14}` | large-army-at-territory |

### 0x40 path — fortress + march-out

Two reachable sub-paths gated on `descriptor[+4] & 0xf`:

  - **nibble == 0** (fortress): `army[+0x07]` is the fortress slot
    byte.  If 0, return.  Otherwise:
    ```
    sprite_idx = army[+0x07] * 8 - 8 + overlay_base_index
    kind = 2  (48-px dirty rect)
    view 0: dx=0x1a, dy=-0x1c
    view 2: dx=6,    dy=-0x15
    view 1: dx=dy=0
    ```
  - **nibble == 2** (march-out): `army[+0x1ad]` is the march-out
    slot byte.  If 0, return.  Otherwise:
    ```
    sprite_idx = 0x81  (fixed)
    kind = 0  (no dirty rect)
    view 0: dx=0x10, dy=-0x12
    view 2: dx=6,    dy=-0x15
    view 1: dx=dy=0
    ```

Other nibble values: stale-sprite dispatch (quirk #70).

### 0x10 path — animation-end (single sprite)

```
if (view_mode == 2) return;
if (first_byte != 0x13) return;
if (animation_tick > 0xf) return;
sprite_idx = animation_tick + 0x28
dx=0xc; dy=-0xe; kind=1
```

This is the same end-frame shape used by the 0x80-only categories
3/6/9/12 — but here it triggers without the tier-byte gate, on a
SPECIFIC `first_byte` value (0x13).

### 0x20 path — directional arrows

Two sub-bands selecting from an arrow-marker base sprite:

| `first_byte` | branch | sprite_idx                                |
|--------------|--------|-------------------------------------------|
| 0x10..0x12   | low    | `(first-0x10)*6 + arrow_base + 0x67`      |
| 0x14..0x16   | high   | `(first-0x14)*6 + arrow_base + 0x55`      |
| 0x13         | (high, filtered) | byte underflow → return (quirk #73) |
| else         | range gate trips first | return                       |

  - Low sub-branch: dx=dy=0, kind=2.
  - High sub-branch: dx=4, dy=-4, kind=2.
  - View-mode 2: skip entire branch.

### 0x80-only path, first_byte < 13 — unit-category state machine

Four per-band state machines (cat 0..3, 4..6, 7..9, 10..12), each
holding three sub-states: **step**, **end-frame**, **idle**.

| cat | role    | action                                                    |
|-----|---------|-----------------------------------------------------------|
|  0  | step    | band-0 step: read pair (+0x2b8, +0x2bc), wrap at >0x2d→0x1e |
|  1  | idle    | return — no frame change, no draw                         |
|  2  | step    | (same as cat 0)                                           |
|  3  | end     | write 0x2e to [+3]; if tier ≥ 3 ∧ tick ≤ 0xf → dispatch   |
|  4  | idle    | return                                                    |
|  5  | step    | band-1: pair (+0x2e8, +0x2ec), wrap at >4→1               |
|  6  | end     | write 9 to [+3]; if tier ≥ 3 ∧ tick ≤ 0xf → dispatch      |
|  7  | idle    | return                                                    |
|  8  | step    | band-2: pair (+0x2d0, +0x2d4), wrap at >0x12→10           |
|  9  | end     | write 0x13 to [+3]; tier+tick gate → dispatch             |
| 10  | idle    | return                                                    |
| 11  | step    | band-3: pair (+0x2a0, +0x2a4), wrap at >0x1c→0x14         |
| 12  | end     | write 0x1d to [+3]; tier+tick gate → dispatch             |

**Step shape**:
```
distance = army[cur_off] - army[prev_off]    // signed 32-bit diff
rate = if (distance < 10)        tier_0_flag
       else if (distance < 0x19) tier_1_flag
       else if (distance < 0x32) tier_2_flag
       else                      tier_3_flag
frame = descriptor[+3]
if (rate != 0) {
    frame += 1
    if (frame > wrap_limit) frame = wrap_to
}
descriptor[+3] = frame
return  // ← steps NEVER dispatch
```

The four tier flags are GLOBAL booleans (see quirk #74) — they bin
armies into rate classes per frame but the actual advance is a
global toggle.

**End-frame shape** (cat 3/6/9/12):
```
descriptor[+3] = end_frame_value    // committed BEFORE gates! (quirk #72)
if (army[tier_off] < 3) return      // dispatch suppressed but frame committed
if (animation_tick > 0xf) return    // same
sprite_idx = animation_tick + 0x28
dx=0xc, dy=-0xe, kind=1
// → fall through to dispatch tail
```

The `tier_off` byte is the SAME field the step branch reads — but
interpreted as an int8_t (signed-byte cast).  Threshold is `>= 3`.

View-mode 2 skips the entire `first_byte < 13` path.

### 0x80-only path, first_byte ∈ [13, 0x19] \ {0x14} — large-army-at-territory

Combines a sprite dispatch with an optional label sub-renderer call:

```
if (army[+0x1bc] (int) == 0) return            // territory link
terr_idx = army[+0x1bc] (int)
next_byte = territory[terr_idx][+0x19a]        // linked next-record
if (next_byte != 0) {
    label_value = territory[next_byte][+0x19c]
    if (view_mode == 0) army_render_label(label_value, 8, -0x38)
    if (view_mode == 2) army_render_label(label_value, 2, -0x28)
    // view_mode 1: no label
}
owner = territory[terr_idx][+0x02]
sprite_idx = owner * 8 - 8 + overlay_base_index
kind = 2
view 0: dx=0x1a, dy=-0x1c
view 2: dx=6,    dy=-0x15
view 1: dx=dy=0
```

### Army-label sub-renderer (`FUN_00407f82`)

548 B helper called only by the 0x80-only large-army branch above.
View-mode-0-only — view-modes 1 and 2 cause an immediate no-op
return.  Stages fixed sprite 0x82, validates `src_off ≤ 310000`
(LOOSER than the army drawer's 70000 main gate — quirk #71),
dispatches via the RLE family with the standard overlay clip,
then conditionally draws a centered decimal text label below the
sprite using the debug font.

Cursor save / restore is faithful: the cursor is bumped by (dx,
dy) before staging and restored before the text render, then the
text render uses (`saved_x + dx`, `saved_y + dy + 10`) as its
position.  The 32-px dirty-rect mark uses the SAVED cursor + a
+4 X-bump.

The text-render skip gate is `label_y > 0x18` — labels positioned
at or above the top clip are not rendered.

### Common dispatch tail

When any branch sets a sprite_idx and falls through:

```
if (phase == 1) dx -= x_half_step       // phase-1 X anchor shift

stage_descriptor(overlay_blob, sprite_idx)
if (src_off > 70000 || src_off < 1) {
    error_count += 1
    return
}
save cursor
cursor += (dx, dy)
if (kind == 1) mark_dirty_32(cursor >> 4)
if (kind == 2) mark_dirty_48(cursor >> 4)
apply_overlay_clip()                    // X = [cam_x0, 0x1de], Y view-aware
dispatch_sprite_rle(overlay_blob)
restore cursor
```

For the stale-sprite case (quirk #70), the port pulls
`gfx_dispatch_last_glyph_index()` to substitute when no branch set
a sprite_idx.  Retail dispatches with whatever `DAT_005c9288`
already held.

## Debug overlay text (`FUN_00408c50`)

Two-mode developer overlay.  Both modes default off (production
builds keep both at 0).

### Mode 1 — per-cell size value

When `dev_flag_cell_value != 0` AND `dev_flag_descriptor_value == 0`:

    idx       = (cell + (cell >> 31 & 7)) >> 3;
    raw_value = per_cell_size_table[idx];
    if (raw_value < 0) draw_at(cursor + (0x18, 10), abs(raw_value), color=0xf9);
    else               draw_at(cursor + (0x18, 10), raw_value,      color=0x20);

### Mode 2 — descriptor head-link byte

When `dev_flag_descriptor_value != 0` (wins over mode 1):

    head_link = descriptors[cell + 5];
    if (head_link != 0)
        draw_at(cursor + (0, -0x1e), head_link, color=0xf9);

Both modes save/restore the cursor.  Both no-op when the cell is
out-of-range (`>= EMPTY`).  Both route through
`gfx_text_label_render_dec` (already ported).

## Bindings

The port exposes these binding setters (mirroring `engine_map_render`'s
shape):

  - `engine_overlay_render_bind_tile_array(cells, row_stride_bytes)`
  - `engine_overlay_render_bind_tile_descriptors(descriptors)`
  - `engine_overlay_render_bind_overlay_blob(blob)`
  - `engine_overlay_render_bind_territory_records(records)`
  - `engine_overlay_render_bind_army_records(records)` *(new — army drawer)*
  - `engine_overlay_render_bind_per_cell_size_table(table)`
  - `engine_overlay_render_bind_camera(x0, y0, col_start, row_start, col_count, row_count, x_step, y_step, x_half_step)`
  - `engine_overlay_render_bind_canvas(pixels, pitch, height)` *(for pre-clear)*
  - `engine_overlay_render_bind_debug_font(font, char_glyph_table)`
  - `engine_overlay_render_set_dirty_state(state)`
  - `engine_overlay_render_set_view_mode(mode)`
  - `engine_overlay_render_set_active_territory(idx)`
  - `engine_overlay_render_set_dev_flags(cell_v, desc_v)`
  - `engine_overlay_render_set_render_mode(mode)` *(new — army filter)*
  - `engine_overlay_render_set_overlay_base_index(base)` *(new — fortress + large-army)*
  - `engine_overlay_render_set_arrow_marker_base_index(base)` *(new — 0x20 path)*
  - `engine_overlay_render_set_animation_tick(tick)` *(new — end-frame sprite)*
  - `engine_overlay_render_set_animation_rate_flags(t0, t1, t2, t3)` *(new — step rates)*

And these counters / errors:

  - `engine_overlay_render_error_count() / set_error_count(n)`
  - `engine_overlay_render_{army, city, debug}_call_count()`
  - `engine_overlay_render_reset_call_counts()`

The army drawer reads the army-record array at the offsets
documented in the header's `bind_army_records` doc-comment and the
territory-record array at +0x02 (owner), +0x19a (next-link), +0x19c
(label-value).

## Open questions

  - **What re-arms bit 0x40 of descriptor[+2]?**  The city drawer
    clears it post-dispatch, but the next frame still needs to draw
    cities, so something re-sets it.  Save-load path?  City-state
    change handler?  Frame-edge "visible mark" pass?  Now that the
    army drawer is ported, the next adjacent caller (`FUN_0040526e`,
    the parent of both passes) is the most likely place — survey it
    to confirm.
  - **What does the first descriptor byte's `0x14` value represent?**
    The city drawer's large-select gate `(raw & 0x80) && (first != 0x14)`
    AND the army drawer's "large army at territory" branch both
    EXCLUDE 0x14.  The 0x20 arrow path additionally filters 0x13 via
    byte underflow (quirk #73).  So `first_byte ∈ {0x13, 0x14}` are
    reserved sentinels.  Likely 0x13 = "transition completing" and
    0x14 = "small village / outpost" — needs editor cross-reference.
  - **Is the pre-clear strip a known DDraw workaround?**  Quirk #69
    documents the view-mode-1-only pre-clear.  Could be a
    DDraw-version-specific bug or a true game-logic invariant.
  - **What drives the four tier flags?**  Quirk #74 — they're
    globals that the army drawer reads but doesn't write.  Their
    setter is somewhere in the main loop (probably FPS-throttled).
    Worth locating before the gfx.c runtime wire-up so animations
    look right at correct rates.
  - **Per-army record field semantics.**  The (cur, prev) pairs at
    +0x2a0, +0x2b8, +0x2d0, +0x2e8 are clearly animation drivers,
    but what each `cur` actually counts (positions? hit-points?
    morale?) is unknown.  Cross-reference with the game-logic
    update functions (probably named `FUN_004???` adjacent to the
    army record schema) to surface.
