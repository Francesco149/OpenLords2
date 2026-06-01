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

## The strategic map is THREE walker clusters — this is only the FEATURE layer

Validated 2026-06-01 against retail's live map state via the parity
harness (`tests/map_render_harness.c` + `tools/map_render_parity.py`,
fed by `frida_capture.py --dump-regions`; capture run
`runs/capture/strat-blobs1`).  The map-draw wrapper **`FUN_004050c0`**
(called from the strat-map composition `FUN_0040f5fd`) renders the
overworld in three passes over the SAME 2-D cell array, in this order:

| pass | frame driver | row iterators        | per-tile drawers     | reads |
|-----:|--------------|----------------------|----------------------|-------|
| 1 (base terrain)    | `FUN_0040526e` (537 B) | `FUN_00405ae9`/`FUN_00405c2f` | `FUN_004063c1`/`FUN_00406673` | descriptor +1/+3/+7; blobs `0x568208`,`0x53e914`,`0x57d344`,`0x55409c`,`0x56898c`,… |
| 2 (overlay terrain) | `FUN_00405602` (608 B) | `FUN_00405eb5`/`FUN_00405fac` | `FUN_004071a0`/`FUN_004081a6` | (analogous) |
| 3 (**features**)    | `FUN_00405487` (379 B) | `FUN_00405862`/`FUN_004059af` | `FUN_00408438` (2072 B) | descriptor +2 flag/+5 head; records via `g_per_territory_420`; blobs primary/alt/overlay |

**Only pass 3 is ported** (this `engine_map_render` cluster).  The
per-tile drawer `FUN_00408438` reads the linked-list HEAD at
`descriptors[cell + 5]` and **returns immediately when head == 0**
(retail `0x408483: cmp var_24h,0 / je 0x408c4b`).  In the lastturn.sav
frame, 299 of 300 visible cells have head == 0 — i.e. plain terrain
carries no territory record, so the feature walker correctly draws
NOTHING for it.  The terrain grass/water/rivers/roads you see on the
overworld come from passes 1 and 2 (the base + overlay terrain
walkers), NOT from this cluster.

**Parity-harness result (feature layer):** binding retail's captured
camera + descriptors + records + 3 blobs and running
`engine_map_render_frame`, the harness reports `error_count=0
call_count=291` and draws exactly ONE sprite — a merchant wagon
(record #2, type 3, sprite-idx 12, w40×h32, src_off 18056) at cursor
(186,215) — the sole feature in the visible window.  This validates the
ported walker against real data: it dispatches the one real feature
byte-coherently and skips the 290 head-0 terrain cells, matching
retail's control flow exactly.  (The wagon happens to sit under the
"Non 256 colour display" scroll popup in the captured retail frame, so
a direct pixel-diff of that sprite is occluded; the validation is the
0-error coherent dispatch at the retail-derived position.)

**Next frontier:** port passes 1 + 2 (the two terrain walker clusters)
to draw the actual visible overworld.  New dependency: their per-tile
drawers read base-tile sprite banks via the `0x568208`/`0x53e914`/…
globals (loaded by the season `.pl8` loader `FUN_004984dc`, deferred) —
the parity harness will need those banks captured (`--dump-regions`
deref) the same way the feature blobs were.

### Pass 1 (base terrain) frame driver `FUN_0040526e` — structure

Parallel to the feature frame driver `FUN_00405487` (top phantom row →
phase1 row → `(row_count-2)/2` × (phase0,phase1) pairs → bottom phantom
row), walking the SAME camera globals (cam_x0 `0x52afdc`→`0x591524`,
cam_y0 `0x553244`→`0x591528`, row cursor `0x5651b8`→`0x56d594`, col
cursor `0x5651b4`→`0x56d598`, `0x56957c`=0, advances by x_step `0x568220`
/ y_step `0x53f65c`).  Two structural differences from pass 3:

1. **Each cell calls THREE drawers, not one**, and the empty/non-empty
   split is INVERTED vs the feature walker:
   - non-empty cell (`< 0xfff0000`): `FUN_004063c1(phase)` then
     `FUN_00406673(phase)` — two stacked terrain sprite layers
     (ground + decoration), each reading descriptor fields +1/+3/+7.
   - **empty cell (`>= 0xfff0000`)**: NOT skipped.  It stores
     `0x5c9288 = cell - 0xfff0000` (or 0 when render_mode `0x53f264`==1)
     — i.e. the low 20 bits of the empty marker are a sprite index —
     then calls `FUN_0042a7f1(phase)`, the **sky/border/off-map tile
     drawer**.  This is why the feature walker (which `return`s on these
     cells) leaves them blank but the real map shows water/sky/edges.

2. Phase arg is a literal pushed per call: top phantom row pushes **2**,
   bottom phantom pushes **1** (the inner rows get their phase from the
   row iterators `FUN_00405ae9`/`FUN_00405c2f`).

So pass 1 alone is a 6-function cluster: frame `FUN_0040526e`, row iters
`FUN_00405ae9`/`FUN_00405c2f`, drawers `FUN_004063c1`/`FUN_00406673`/
`FUN_0042a7f1`.  Pass 2 (`FUN_00405602` → `FUN_004071a0`/`FUN_004081a6`,
iters `FUN_00405eb5`/`FUN_00405fac`) is the analogous overlay cluster.
The `0x5c9288` global is the per-tile "empty-marker sprite index" the
sky drawer consumes (the feature walker has no equivalent).

### Pass 1 drawers — descriptor fields, blob selector, blitters

**Terrain-A drawer `FUN_004063c1`** (690 B), per non-empty cell value
`DAT_00567954` (raw byte offset into the descriptor table at `0x522f90`):
- reads descriptor **+1** → `DAT_0057d374`, **+7** → `DAT_0057d384`,
  **+3** → `DAT_005c9288` = the sprite INDEX into the chosen blob.
- descriptor **+2** flag byte `& 0x1c` selects WHICH terrain blob:
  `0x00`→`DAT_00568208`, `0x04`→`DAT_0053e914`, `0x08`→`DAT_0057d344`,
  `0x0c`→`DAT_0055409c`, `0x10`→`DAT_0056898c`; any other value of the
  3 bits → return (no draw).  (render-mode gate: mode-1 + flag&0x20==0 →
  index 0 / blob 0.)
- flag **bit 0** is a one-shot "first paint" marker: if set, clears it and
  marks dirty via `FUN_00452628(cursor_x>>4, cursor_y>>4)` (= ported
  `engine_dirty_rect` helper).
- stages the sprite descriptor `blob + index*0x10 + 8`: width→`DAT_005c9258`,
  `DAT_005aeb70`, **src_off→`DAT_0058fe04`** (validated `< 0x4baf1`
  =310001 and `>= 0`, else bump `_DAT_0058fea8`), byte+6→`DAT_00591568`,
  byte+0xd→`DAT_005bb478`, byte+7→`DAT_0057d3c0`.
- dispatches by `param_3` (phase) × view-mode `DAT_0057cb18`:
  phase3→`FUN_0045337b`(v0)/`FUN_00463c06`(v2); phase4→`FUN_004538f6`(v0)/
  `FUN_00463c62`(v2); else→`FUN_00452820`(v0)/`FUN_00463b42`(v2).

**Sky/empty drawer `FUN_0042a7f1`** (224 B): same staging but ALWAYS from
blob 0 (`DAT_00568208`) at index `DAT_005c9288` (the empty-marker low bits
set by the frame driver), dispatching `FUN_00452820`(v0)/`FUN_00460501`(v1)/
`FUN_00463b42`(v2).  No phase/flag logic.

**The blitters are all UNPORTED** and are the bulk of pass 1:
`FUN_00452820` **2907 B**, `FUN_004538f6` 1403 B, `FUN_0045337b`,
`FUN_00460501`, `FUN_00463b42`/`c06`/`c62`.  For the captured **view-mode 0**
frame only `0x452820` + `0x4538f6` + `0x45337b` are needed.

**`FUN_00452820` is a fully-unrolled isometric-tile blitter** — no RLE, no
palette logic, just ~450 direct `*(dest+OFF) = *(src+IDX)` copies (2/4-byte):
- `dest = DAT_004ea1a8 (fb base) + DAT_00591524 (cursor_x) + DAT_00591528
  (cursor_y) * DAT_004eb274 (pitch)`; `src = param_3 (blob) + DAT_0058fe04
  (src_off)`.
- **Pitch `DAT_004eb274` = 640** (captured `strat-terrain1`).  The unrolled
  dest-offset rows step by **`0x27e` = 638 = pitch − 2**, i.e. each tile row
  is drawn one scanline down AND 2 px left — a **left-leaning parallelogram
  shear** (the iso terrain tile).  Tile row r relative to cursor lands at
  `(cursor_x + 0x1c − 2r, cursor_y + r)`.  A pitch-640 canvas in the harness
  is therefore correct.
- `param_4` (phase) gates a `!= 2` (full copy) vs `== 2` variant branch.

**Port plan (byte-exact, table-driven):** parse the decompile's copy list
into a `(dest_off, src_idx, size)` table and replay it in a compact loop —
exact by construction, and the same extractor handles `0x4538f6`/`0x45337b`.
This avoids hand-transcribing 450 unrolled statements.

### Captured terrain dataset — `runs/capture/strat-terrain1`

Clean (popup-free, via `tools/traces/load_map_clean.jsonl`) view-mode-0
frame.  `dump/` holds tile_cells, descriptors, territory_records, the 3
feature blobs, and the **5 terrain blobs**: `terr_blob0` (`0x568208`,
323480 B, base terrain) / `terr_blob1` (`0x53e914`, 57304) / `terr_blob2`
(`0x57d344`, 195136) / `terr_blob3` (`0x55409c`, ≥512K clamped) / `terr_blob4`
(`0x56898c`, ≥512K clamped).  `globals_dump.json` adds `fb_pitch`=640.
Diff target = `runs/capture/strat-spray1/frames/frame_00009.bmp` (clean map).

### PASS-1 DRAWER-A PORTED + FRAME-VALIDATED (2026-06-01) — 90.9% pixel-exact

`src/engine_map_terrain.{c,h}` ports the pass-1 cluster's frame driver
(`FUN_0040526e`), both row iterators (`FUN_00405ae9` aligned phase-0,
`FUN_00405c2f` half-shifted phase-3/0/4), terrain **drawer A**
(`FUN_004063c1`, base ground) and the **sky** drawer (`FUN_0042a7f1`),
dispatching the table-driven view-mode-0 blitters
(`FUN_00452820`/`0045337b`/`004538f6`) replayed byte-exactly from
`src/gen/blit_table_*.h`.  The C harness
(`tests/terrain_render_harness.c`, `make -C tests terrain-harness`,
driven by `tools/terrain_render_parity.py`) renders the captured frame
and scores **209470/230400 = 90.9% pixel-exact** vs the clean retail
frame over the 480×480 map region, with **0 OOB copies, 0 bad src
offsets, error_count 0** — validating the whole data path (descriptor
layout, blob select, sprite-directory `src_off` at `blob+idx*0x10+0xc`,
camera walk, palette).  A Python prototype (`tools/proto_pass1_render.py`)
produced the identical numbers first.

**Descriptor / sprite-directory facts nailed down:** for cell value `cv`
(raw byte offset into the descriptor table @ `0x522f90`):
`desc[cv+2]` flag (bits `0x1c` select blob 0..4: `0x00/04/08/0c/10`;
bit `0x20` render-mode mask; bit `0x1` first-paint dirty, elided in the
single-frame port), `desc[cv+3]` = sprite index.  Sprite directory entry
= `blob + idx*0x10`; the unrolled blitter needs only `src_off = i32 @
entry+0xc`, validated `0 ≤ src_off < 0x4baf1`.  Phase→blitter:
0→`452820`(A+B), 1→`452820`(A only, bottom phantom), 2→`452820`(B only,
top phantom), 3→`45337b`(full, left edge), 4→`4538f6`(full, right edge).
Empty cells (`≥0xfff0000`): low 20 bits = blob-0 sprite index → sky
drawer; inner-iterator gate `DAT_0053e9ac!=0 || idx>6` (phantom rows
ungated).  The captured interior frame has **0 empty cells**.

**REMAINING 9% = drawer B (decorations).** `FUN_00406673` is the second
stacked layer (trees/hills/roads on top of base ground).  It is **STUBBED**
(cells counted via `drawb_skipped`, not drawn): 263 cells would draw.  It
pulls in a SEPARATE, un-ported blitter family that takes the column-Y
accumulator `DAT_0056957c` and keys on directory fields `DAT_00591568`
(kind ∈ {2,3,4}) + `DAT_005bb478` (count, drawn only when `!=0`):
`FUN_00455f04`/`459258`/`453e71`/`4580a9`/`456fd8` (view-mode-0 arms) — a
second `src_off` is `_DAT_005aeb6c = src_off + 900` (v0).

**CONFIRMED: drawer B is the tall-sprite OVERHANG layer (the "bleed-over"
into neighbouring cells), not a draw-order effect.**  A tall terrain
sprite (mountain/hill/forest) is stored as TWO runs in one blob entry:
the base diamond at `src_off` (drawn opaquely by drawer A — the part we
render) PLUS a **transparent overhang strip at `src_off + 900`** (v0).
The drawer-B blitters (e.g. `FUN_00453e71`, 8339 B) read `param_3 +
_DAT_005aeb6c` and write to the SAME canvas at the SAME cursor
(`DAT_004ea1a8 + DAT_00591524 + DAT_00591528*pitch`) but at large
positive dest offsets (e.g. `pcVar3[0x2080]` ≈ 13 rows down) inside a
`do{…}while` loop with per-pixel zero-skip (`if (*p!=0) dst=*p;`) — i.e.
a clipped transparent strip blitter, NOT a fully-unrolled fixed copy, so
NOT table-extractable.  Because the overhang rises into cells already
painted (and the base diamonds of later cells do not cover it in retail),
omitting drawer B makes tall features look flat/cut-off and lets a
neighbour's base field sit where the overhang should be.  The pass-1 walk
ORDER is correct (byte-identical to retail's frame driver); the missing
pixels are purely drawer B.  **NEXT = RE + port drawer B's transparent
strip-blitter family**, keyed on `DAT_00591568` (kind 2/3/4) ×
`DAT_005bb478` (strip count, `!=0`) × view-mode, then re-diff toward 100%.  After that: pass 2
(`FUN_00405602` overlay cluster) + compose all three passes + wire
screen-dispatch arm 0.

### CORRECTION (2026-06-01) — the missing 9% is NOT drawer B

Empirical zoom-diff of the proto vs the clean retail frame (NEAREST-upscaled
truth | mine | diff crops, not pixel samples) overturns the drawer-B overhang
diagnosis above:

* **Top band (rows 0–23, full 640px width, ~10.5k px ≈ half the miss) = the
  "File / Options / Help" MENU BAR**, drawn by a separate menu/HUD function.
  Pass-1 tiles only paint DOWNWARD from the cursor (block A = cursor rows
  0..14, block B = 15..29; all dest offsets positive) and the walk's cursor
  never rises above `y0 = 9`, so pass 1 *cannot* produce those rows. Scoring
  them against a terrain-only render was the bulk of the artificial loss.
* **In-body "wrong" pixels (~8.7k) = map FEATURES** (cattle / wagons / units)
  plus pass-2 overlay — absent only because `proto_pass1_render.py` runs pass 1
  alone. The base terrain underneath matches cleanly. The feature walker is
  already ported (`engine_map_render`).
* **Right edge = a 2px viewport boundary.** Negligible.
* **Drawer B draws NOTHING in this frame.** For all 29 valid drawer-B cells the
  row-clip `param_4 = DAT_0056957c` (col-Y accumulator, 105–300) far exceeds the
  sprite `count = DAT_005bb478` (3–17), so the blitter guard `param_4 <= iVar1`
  (with `iVar1 < count`) never fires. The overhang is clipped away entirely at
  this zoom/camera. (The drawer-B blitter family was analyzed — a sliding-window
  transparent diamond strip-blitter; net effect = each source row drawn once at
  `base − row*pitch` with the block-0 shape, looped `[param_4, count)` — but it
  is not needed for this frame.)

Excluding the menu bar, the pass-1 proto scores **95.2%** over the map region;
the residual is features + pass-2 overlay, i.e. the *other two clusters*, not
pass 1. **Corrected next step: compose all three clusters (pass-1 base ✓ +
pass-2 overlay + the ported feature walker) and diff — that should leave only
HUD chrome (menu bar + right realm panel) as the gap.**

### DRAWER-B OVERHANG COMPLETE (2026-06-01) — kind-3/4 + kind-2 phase-3 ported, drawB_skip → 0

Supersedes the "draws NOTHING" CORRECTION above (that was a pixel-sample
artefact; zoom-diff later showed drawer B *does* draw the tall-sprite overhang —
see the r2-corrected-guard finding). The full drawer-B family is now ported:

* **kind 2, phase 0/1** → `FUN_00453e71` (pyramid branch-A table replay,
  `extract_drawb_table.py` → `drawb_table_453e71.h`). Landed earlier.
* **kind 2, phase 3** → `FUN_00455f04`
* **kind 3, phase 0/1** → `FUN_004580a9` (param_5=0)
* **kind 4, phase 0/1** → `FUN_00459258` (param_5=0)

**Key RE finding — the PYRAMID (branch A) is dead; the flat STRIP (branch B) is
the live path.** Every drawer-B blitter has the shape
`do { if (param_4 <= iVar) { <pyramid>; break; } dst -= pitch; <flat strip>;
src += stride; iVar++; } while (iVar < count);`. r2-verified the guard on all
four (`vendor/original/Lords2.exe`): `cmp ebx,ecx; jle <pyramid>` with
ebx=param_4=`DAT_0056957c` (col_y_accum, 105-300) and ecx=iVar (0..count,
count<0xc9 but 3-17 in practice). Since `param_4 >> count`, `jle` never fires →
the pyramid never runs → the **flat strip runs exactly `count` times**. So
porting branch B is faithful to the live path (and far simpler than branch A's
multi-pointer / horizontal-step pyramid that the earlier session-notes feared).

**Branch-B replay** (`drawb_strip_flat` in `engine_map_terrain.c`): the dst
pointer is decremented one scanline *before* the first copy, so row r writes at
`base - (r+1)*pitch`; src base advances `stride` per row (455f04: 0x3a; 4580a9 /
459258: 0x1e). No depth gating — each row draws the full strip; the diamond
shape comes from the dst_off spread + zero-skip transparency. The dispatcher
`FUN_00406673` selects blitter by (kind=`DAT_00591568`, phase=param_3,
view-mode=`DAT_0057cb18`) and the `param_5` left-edge shift (2 for edge phases
3/4, 0 for aligned 0/1).

**Table extraction:** `tools/extract_drawb_stripB.py` parses branch B (from the
unique `dst = dst + -DAT_004eb274;` line to `} while`) into flat
`{src_idx, dst_off}` tables, folding `init_off` (459258's `+0x1c`) and the
`pcVar4 = pcVar3 + 2` aliasing into dst_off for the param_5=0 variant →
`src/gen/drawb_stripb_{455f04,4580a9,459258}.h`. Regenerate with
`nix develop -c python3 tools/extract_drawb_stripB.py 455f04 4580a9 459258`.

**Validated** (`tools/scene_render_parity.py` vs clean truth
`strat-spray1/frame_00009.bmp`): `drawB=29 drawB_skip=0 oob=0 err=0`,
**99.37% excl menu bar** (was 99.19% with the 6 cells skipped). User confirmed
the **town centre / lumberjack overhang is now 1:1**. Residual diff is the
known sparse ~1px cattle-edge speckle (a separate issue — see memory
`feedback_zoom_dont_sample` lead #1) plus the unported menu bar / realm panel.

**Still unported** (don't appear in the strat-terrain1 frame, counted as
drawB_skip if hit): edge-phase kind-3/4 (param_5=2 variants of 4580a9/459258)
and kind-2 phase-4 `FUN_00456fd8`, plus all view-mode 1/2 arms.
