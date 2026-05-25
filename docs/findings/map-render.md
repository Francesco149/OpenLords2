# Map-tile renderer — territory-sprite walk

This subsystem renders the **strategic-mode overworld map** by walking
the per-tile linked list of *territory records* and dispatching the
appropriate sprite for each.  Four retail functions form the cluster:

| addr        | role                                      | size  |
|-------------|-------------------------------------------|------:|
| `0x405487`  | outer frame driver (top half + drop into) | 379 B |
| `0x405862`  | mid-row "advance one row" iterator        | 333 B |
| `0x4059af`  | alternating row (with phase=1 first tile) | 314 B |
| `0x408438`  | per-tile drawer (the heart of the cluster)| 2072 B|

The three first functions iterate the 2-D map array and call the
fourth per cell.  They are mechanical column-walkers; the fourth is
where each territory's actual sprite gets staged + dispatched.

## Storage layouts

### Per-territory record array — `g_per_territory_420`

Already declared in `src/save.h` (save-schema entry 5).  151 slots of
420 = `0x1a4` bytes each, at retail base `0x52f0b0`.  Slot 0 is unused
sentinel; slots 1..150 hold one territory each.

Map-render reads the following byte/short fields off each record
(offsets from the `g_per_territory_420[idx * 0x1a4]` start, NOT from
retail's pseudo-base `DAT_0052f0b4` which is +4 into the record):

| record offset  | size  | retail symbol         | field meaning                         |
|----------------|------:|------------------------|---------------------------------------|
| `+0x02`        | byte  | `DAT_0052f0b2 [i*0x1a4]` | overlay sprite-index multiplier (for record-type 1, gates whether the second sprite is drawn) |
| `+0x04..05`    | short | `DAT_0052f0b4 [i*0x1a4]` | next-link in per-tile linked list (records the next record's index in the SAME territory array, NOT a pointer; loop terminates when `next == self`) |
| `+0x07`        | byte  | `DAT_0052f0b7 [i*0x1a4]` | primary sprite-index into the blob's descriptor table |
| `+0x08`        | byte  | `DAT_0052f0b8 [i*0x1a4]` | record-type byte (1..4 = drawing variants; 0 = "normal"; 4 = alt-blob) |
| `+0x09`        | byte  | `DAT_0052f0b9 [i*0x1a4]` | orientation byte (raw; mapped into 0..7 by subtracting a global rotation offset and wrap-adding 8) |
| `+0x149`       | byte  | `DAT_0052f1f9 [i*0x1a4]` | frame-offset byte (signed in [-16,+15]; column index into palette tables) |

The other 414 bytes per record carry strategic-mode game state
(owner, army composition, food/gold, etc.).  Map-render doesn't read
them.

### Per-tile descriptor table — at retail base `0x522f90`

A run-time per-tile-bytecode lookup table (16384 entries × 2 B = 32768
B, save-schema entry 0 — the overworld map).  Map-render addresses
two byte fields by adding an offset to the table base:

| retail address      | offset | meaning                                                  |
|---------------------|-------:|----------------------------------------------------------|
| `DAT_00522f92[idx]` | base+2 | per-tile-bytecode flag byte (only bit `0x20` is consumed) |
| `DAT_00522f95[idx]` | base+5 | per-tile-bytecode "first record" — head of the linked list |

The flag byte's `0x20` bit gates rendering when the engine is in a
particular submode (when `DAT_0053f264 == 1`, only tiles with the bit
set are drawn — likely a "fog of war / explored" mask).  The head
byte is 0 when the tile has no territories on it (then the loop
exits immediately).

Note: `DAT_00567954` (the cell value loaded from the 2D map array) is
used DIRECTLY as a byte offset into this table.  The per-row sweep
reads a 32-bit int from the map array; values `>= 0xfff0000` mean
"empty cell, skip drawing" and are filtered by the iterators before
they call into the per-tile drawer.

### 2-D map array — at retail base `0x55cea0`

The "cell value" array iterated by the row helpers.  Layout:

  - Each cell: 4-byte `int` tile-bytecode.
  - Row stride: `0x104` bytes = 65 ints per row.
  - Iteration starts at `(camera_row = DAT_005651b8, camera_col = DAT_005651b4)`
    and walks `DAT_0053e8ac` columns per row.

### Palette tables — six 128-byte tables at `0x4d8108..0x4d8388`

Three view-modes (zoom-out, zoom-mid, zoom-in — selected by
`DAT_0057cb18` ∈ {0, 1, 2}), each with an X-displacement table and a
Y-displacement table:

| retail addr | view-mode | axis |
|-------------|----------:|------|
| `0x4d8108`  | 0         | X    |
| `0x4d8188`  | 0         | Y    |
| `0x4d8208`  | 1         | X    |
| `0x4d8288`  | 1         | Y    |
| `0x4d8308`  | 2         | X    |
| `0x4d8388`  | 2         | Y    |

Indexing: `table[orientation * 0x10 + frame_offset]` where
`orientation` ∈ [0, 7] (post-rotation, post-wrap) and `frame_offset`
is the per-record byte at +0x149 (signed).  Reads a signed-byte
displacement (sign-extended via the decompiler's `(char)` cast).

The tables are stored static-const in retail's .rdata and ARE
read-only.  Mirrored as a static `const int8_t[3][2][128]` block in
the port.

## Outer iterator structure

Hex-grid offset-coordinate map.  Even rows draw at one X origin, odd
rows shift by half a tile horizontally (`DAT_00567950`).  The "phase"
arg passed to the per-tile drawer (0, 1, or 2) controls the X
adjustment:

  - phase 0: X offset = +DAT_00567950 (half-step right)
  - phase 1: X offset = -2                       (small shift left)
  - phase 2: X offset = +DAT_00567950 - 2        (half-step right minus 2)

The 379-byte top-level driver `FUN_00405487` does:

```
Y = DAT_00553244;            // camera initial Y (sky offset)
X = DAT_0052afdc;            // camera initial X
camera_row = DAT_005651b8;
sky_y = 0;
camera_col_start = DAT_005651b4;

// Pass 1: top phantom row (drawn at top edge, NOT phase-shifted)
for (c = 0; c < column_count; c++) {
    cell = map[camera_row * 65 + (camera_col_start + c)];
    if (cell < 0xfff0000) draw_tile(phase=0);
    X += DAT_00568220;           // x_step per column
}
Y     += DAT_0053f65c;            // y_step per row
camera_row += 1;
phase1_row();                     // one alternating row (phase=1 first)
for (r = 0; r < (row_count - 2)/2; r++) {
    phase0_row();                 // FUN_00405862
    phase1_row();                 // FUN_004059af
}

// Pass 2: bottom phantom row (mirrors Pass 1 logic)
X = DAT_0052afdc;
camera_col_start = DAT_005651b4;
for (c = 0; c < column_count; c++) { ... draw_tile(0); X += x_step; }
```

The pattern is "two phantom rows + a stack of (phase0_row,
phase1_row) pairs in between".  This is the classic hex-grid render
order — odd/even rows offset by half a tile, neighbours visited in
column-major order within each row pair.

## Per-tile drawer (`FUN_00408438`) call topology

Pure consumer of already-ported subsystems:

  - **dirty-rect cluster** — `engine_dirty_rect_mark_tile_{32,48,64}`
    based on the record-type adjust byte `local_24`.
  - **dispatcher stagers** — `gfx_dispatch_stage_sprite_descriptor`,
    `gfx_glyph_set_dest`, `gfx_dispatch_clip_x`, `gfx_dispatch_clip_y`,
    `gfx_dispatch_apply_skip_and_dispatch_sprite` (for the FIRST
    sprite family — raw blitters).
  - **dispatcher RLE stager** —
    `gfx_dispatch_dispatch_sprite_rle` (for the SECOND sprite —
    the optional overlay drawn from a separate RLE-encoded blob).

For each record in the per-tile linked list:

  1. Resolve sprite-blob base: primary `DAT_00553224`, OR
     alternate `DAT_0056d8bc` if record-type byte == 4.
  2. Compute `local_10 = (orientation - DAT_00522f7c + 8) % 8` —
     rotation lookup index.
  3. Look up `(dx, dy)` from the view-mode-selected palette tables
     using `local_10 * 16 + frame_offset_byte`.
  4. Apply phase shift: phase 1 ⇒ `dx -= 2`, phase 2 ⇒
     `dx += step_half - 2`, otherwise `dx += step_half`.
  5. Apply record-type adjust (1/2/3/4 each shift `(dx, dy)`
     differently and set `local_24 = 3`, which selects the 64-px
     dirty-rect box; default = 32-px).
  6. Save `(cursor_x, cursor_y)` from the camera, then set new
     cursor to `(camera_x + dx - sprite_w/2, camera_y + dy - h_short)`.
  7. Validate descriptor: `src_offset > 310000` OR `sprite_w < 1` ⇒
     bump `_DAT_0058fea8` (error counter), return.
  8. Mark dirty rect at `(cursor_x>>4, cursor_y>>4)` with the
     appropriate box size.
  9. Set clip (canvas-wide X; alt-clip-aware Y).
 10. Bake skips into `src_offset`, dispatch first-family blitter.
 11. If record-type is 1 (with overlay byte != 0) or 2, compute an
     overlay sprite descriptor on the SAME (cursor_x, cursor_y) base
     with a small offset, stage from the overlay blob (`DAT_0055ce5c`),
     dispatch RLE-family blitter.
 12. Restore `(cursor_x, cursor_y)`, advance linked list, repeat.

## Quirks worth noting

  - **Record-type 4 swaps the descriptor BASE**, not just an offset.
    Other types share `DAT_00553224`; only type 4 reads from
    `DAT_0056d8bc`.  Verified by re-reading the conditional at
    decompiled-line 22.
  - **Loop termination is `next == self`** (self-loop), not
    `next == 0`.  Records in a linked list always have a non-zero
    next-link; the LAST record points to itself.
  - **Two anchor adjusts cumulate**: `local_24` becomes 3 when the
    record-type is 1/2/3/4 (any of the four "decorated" variants),
    selecting the 64-px dirty-rect box.  Default (type 0) leaves
    `local_24 = 0`, selecting the 32-px box.  There's no 48-px path
    in this function — the 48-px helper (`engine_dirty_rect_mark_tile_48`)
    is reached via `local_24 == 2` which `FUN_00408438` never sets.
  - **Y-clip varies on view-mode**: at view-mode 1 the alt-clip
    bottom is 0x1d8 (= 472); at view-mode 0 or 2 it's 0x1da (= 474).
    The 2-pixel difference matches the row-pair stride difference
    between zoom levels.
