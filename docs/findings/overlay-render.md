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
| `0x4071a0`  | per-tile drawer — units / armies (bit 0x80)   |3179 B *deferred* |

Five of six functions ported this chip; the army drawer is deferred
because of size + complexity (it has unit-category state-machine logic
that animates frames in-place).  In the current port it's a stub that
bumps a per-call counter for test instrumentation.

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

## Army drawer (`FUN_004071a0`) — DEFERRED

3179 B per-tile drawer with state-mutating logic — animates unit
frames in-place by writing to `descriptors[cell + 3]` based on the
unit's current state and a per-category animation rate.  Multiple
unit-category switches (chasing, fighting, marching, dying).

This chip exposes it as a no-op stub that bumps a `g_army_call_count`
counter so the row iterators can be tested for dispatch fidelity.
Real body lands in a follow-up chip.

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
  - `engine_overlay_render_bind_per_cell_size_table(table)`
  - `engine_overlay_render_bind_camera(x0, y0, col_start, row_start, col_count, row_count, x_step, y_step, x_half_step)`
  - `engine_overlay_render_bind_canvas(pixels, pitch, height)` *(for pre-clear)*
  - `engine_overlay_render_bind_debug_font(font, char_glyph_table)`
  - `engine_overlay_render_set_dirty_state(state)`
  - `engine_overlay_render_set_view_mode(mode)`
  - `engine_overlay_render_set_active_territory(idx)`
  - `engine_overlay_render_set_dev_flags(cell_v, desc_v)`

And these counters / errors:

  - `engine_overlay_render_error_count() / set_error_count(n)`
  - `engine_overlay_render_{army, city, debug}_call_count()`
  - `engine_overlay_render_reset_call_counts()`

## Open questions

  - **What re-arms bit 0x40 of descriptor[+2]?**  The city drawer
    clears it post-dispatch, but the next frame still needs to draw
    cities, so something re-sets it.  Save-load path?  City-state
    change handler?  Frame-edge "visible mark" pass?  Likely surfaces
    when porting the army drawer or the game-state subsystem.
  - **What does the first descriptor byte's `0x14` value represent?**
    The city drawer's large-select gate `(raw & 0x80) && (first != 0x14)`
    suggests `0x14` is a settlement-type code meaning "don't draw as
    castle".  Likely a small-village marker.  Would need cross-
    referencing with the editor / save format to confirm.
  - **Is the pre-clear strip a known DDraw workaround?**  Quirk #69
    documents the view-mode-1-only pre-clear.  Could be a
    DDraw-version-specific bug or a true game-logic invariant.
