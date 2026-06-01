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

## RIGHT-PANEL MINIMAP — BYTE-EXACT (2026-06-01, `src/engine_minimap.c`)

The political overview at the top of the right HUD panel.  Ported the
whole chain from the screen-id-0 arm (`FUN_0040f5fd` → `FUN_00410aa9`):

| addr        | port                                | role                                   |
|-------------|-------------------------------------|----------------------------------------|
| `0x46a037`  | `engine_minimap_load_grids`         | read two 128×128 grids from `Map0N.pl8` |
| `0x451bba`  | `engine_minimap_compute_bands`      | per-territory band pre-pass (modes 1-3) |
| `0x410cbd`  | `engine_minimap_plot`               | the 128×128 coloured pixel plot        |
| `0x410aa9`  | `engine_minimap_render`             | backdrop blit + frame + plot + button   |

**Data path.**  `46a037(map_id)` opens `Map0{(map_id>>2)+1}.pl8`
(sub-map `map_id&3`), reads a 4-byte directory entry at
`(sub*0x50)+0xc` → 0x4000-byte **region/territory-id grid**
(`DAT_0052aff0`), then a second entry at `((sub*5+1)*0x10)+0xc` →
0x4000-byte **terrain-type grid** (`DAT_00569590`).  Both via the
already-ported `io_read_file_at` (`FUN_004af8d9`).  Everything else the
minimap needs is **save-backed** and already loaded: per-territory
records `g_per_army_768` (0x300 stride, owner = field `+7`), current
territory `s_slot_0057c93c` (save entry 71), current realm
`s_slot_0057c8cc` (72), minimap mode `s_slot_0057a0c4` (175), territory
count `s_slot_0056d5dc` (77).  The grid holds region ids 0..16 (≤ the
17-record array); land cells reference valid records.

**Plot (`410cbd`).**  For each of the 128×128 cells, skip if the type
grid byte is sea `0x24` or outside `[10,13]`.  Border cells (`type==10`)
of the **current** territory paint white (index `0x20`).  Otherwise:
mode 0 (political) = `MINIMAP_OWNER_COLOR[owner*8 + (type-10)]` (table
`0x4d2900`, 6 owner rows × 8, only the first 4 of each reachable); modes
1/2/3 = `MINIMAP_BAND_COLOR[band]` (table `0x4d28f8`, 6 bands) gated on
the territory being visible to the current realm (field `+5`).  Plot via
`gfx_paint_pixel_clipped` (`FUN_004039c7`).

**Backdrop (`410aa9`).**  The terrain-type grid is blitted raw as a
128×128 sprite via `gfx_sprite_render_raw` (`FUN_0040a01d`, the
**transparent** family — skips index-0 bytes), so the panel's sand
parchment (drawn underneath by the unported `FUN_0040f5fd` sprite
`0x36`) shows through everywhere the grid is 0.  The plot then overlays
the coloured land cells.  Frame/mode-button sprites use the opaque /
transparent blob renderers (`40a567` / `40a21a`).

**VALIDATION — byte-exact.**  `test_minimap_render_political` loads the
real `lastturn.sav`, loads the grids, renders the political minimap, and
`tools/minimap_parity.py` applies `load_clean_dump`'s palette and diffs
the 128×128 box against `frame_00000.bmp`: **8316/8316 drawn pixels =
100.00 %** (the 8068 transparent backdrop cells are the panel sprite's
pixels, scored separately).  The entire England landmass — green
counties, red Lancashire (white-outlined current territory), and the
yellow/magenta/grey/blue realms — is pixel-identical.  Wired into the
drop-in `--strat-load` (`bringup_strat_load`): the exe natively loads the
grids + renders the minimap (`map_id=0 cur_terr=13 realm=1`).  **Only the
sand backdrop differs** (unported `40f5fd` panel chrome), confirmed by
eye.  Remaining right-panel work: `40f5fd` panel sprites (`40a567`
0x36/0x39/0x3b) + the territory-info panel `40f7d3`.

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

### MAP-BODY COMPOSITION DRIVER PORTED (2026-06-01) — engine_map_scene

The three cluster modules (engine_map_terrain pass-1, engine_overlay_render
pass-2, engine_map_render features) were previously composed only by the
Python scene harness.  The retail composition wrapper that screen-dispatch
arm 0 (`FUN_0040f5fd`) calls to paint the map body is now ported as
`src/engine_map_scene.{c,h}`:

```
FUN_004050c0 (53 B)  → engine_map_scene_tick (anim-phase tick + render)
  FUN_004cfb08 (64 B) → GetTickCount 16 ms rate gate, folded into the tick
    FUN_004050f6 (376 B) → engine_map_scene_render_body
       ├ FUN_0040526e → engine_map_terrain_frame    (pass 1 base terrain)
       ├ FUN_00405602 → engine_overlay_render_frame  (pass 2 overlay)
       └ FUN_00405487 → engine_map_render_frame      (pass 3 features)
```

**The actual call chain is `FUN_004050c0 → FUN_004cfb08 → FUN_004050f6`** —
earlier notes called `FUN_004050c0` "the map-draw wrapper", but it only bumps
the 0..3 redraw-phase counter `DAT_0057d380` then calls the rate-limiter
`FUN_004cfb08`, which 16 ms-gates two free-running frame counters
(`DAT_0057d378`, `DAT_0057d388`) before calling the real walker `FUN_004050f6`.
`FUN_004050f6` is what runs the three clusters, in order **pass1 → pass2 →
features** (matching the scene harness).

**KEY RE FINDING — `overlay_base` and `arrow_marker_base` are ANIMATION
sub-phases, not static config.**  `FUN_004050f6` derives eight sub-phases by
right-shifting the two frame counters:

| counter (wrap)        | >>1        | >>2        | >>3        | >>4 (consumed)            |
|-----------------------|------------|------------|------------|---------------------------|
| `DAT_0057d378` (0x80) | `0057d3a8` | `0057d394` | `0057d39c` | `0057d390` = overlay_base  |
| `DAT_0057d388` (0x60) | `0057d3a4` | `0057d3a0` | `0057d398` | `0057d38c` = arrow_marker   |

Only the two `>>4` sub-phases are read by ported code: `DAT_0057d390` (the
"overlay base index" read by BOTH engine_overlay_render and engine_map_render)
and `_DAT_0057d38c` (the "arrow-marker base index" read by
engine_overlay_render).  The scene harness used to feed these the captured
scalar (0); they are in fact `counter>>4` and cycle (overlay_base 0..7 since
the counter wraps at 0x80; arrow_marker 0..5 since its counter wraps at 0x60).
This is the mechanism behind the documented residual anim-phase skew — the
map's small sprites step as these counters advance.  The harness now pins the
counters (`overlay_base<<4`, `arrow_marker_base<<4`) so the phase matches the
captured frame **exactly** (byte-identical output: drawA=263 drawB=29 sky=0
army=14 features=291, **99.37% excl menu bar**), and can sweep phases.

**DEFERRED (next-frame-only, no effect on the rendered frame):** the
column-height accumulator `DAT_0052f020[0..0x20]` and the camera-follow-row
update `DAT_0057c93c` that bracket the cluster calls in `FUN_004050f6`.  The
accumulator is filled by the pass-1 row iterators `FUN_00405ae9` /
`FUN_00405c2f`, which the ported pass-1 collapses, so the follow-row result is
inert; it is computed AFTER the clusters draw and only steers the NEXT frame's
camera centring.  Also the sky-gate decay (`if (DAT_0053e9ac) DAT_0053e9ac--`)
is ported faithfully (via a new `engine_map_terrain_sky_gate_flag` getter) but
only affects the next frame.

Unit-tested in `tests/test_engine_map_scene.c` (9 tests: sub-phase derivation,
counter wraps, 16 ms rate gate, redraw-phase cycle).  **NEXT toward native:**
wire screen-dispatch arm 0 (`FUN_0040f5fd`) to call `engine_map_scene_tick` —
still gated on the PL8 sprite-blob loaders (`FUN_004984dc` season loader +
`FUN_004af8d9` file read) and the HUD chrome fns (menu bar, right realm panel).

### PL8 SPRITE-BLOB LOADER PORTED (2026-06-01) — engine_pl8

The pivot from harness-fed captured blobs to the port loading its OWN sprite
data.  `src/engine_pl8.{c,h}` ports the season loader **`FUN_004984dc`** (the
file-read leaf `FUN_004af8d9` was already ported as `io_read_file_at`,
`io.c`/`io.h`).  `.pl8` banks are **NOT compressed** — `FUN_004af8d9` is a plain
`__open`/`__lseek`/`__read`/`__close`; the iso-tile blitters interpret the raw
file bytes directly.

**Table** (retail `0x4da050`, 64 entries × 0x14: 16-byte NUL-padded filename +
u32 malloc/read size) is extracted by `tools/extract_pl8_table.py` →
`src/gen/pl8_table.h`.  Indexed `table[bank + offset]`, where
`offset = (view_mode==2 ? 0x20 : 0) + (1<=season<=4 ? season*8-8 : 0)`
(`view_mode = DAT_0057cb18`, `season = DAT_0057c934`).  64 entries = 4 seasonal
blocks (1a/1b/1c/1d at offsets 0/8/16/24) + a 4-copy view-mode-2 block
(offset 0x20, all `*2a`).

**Bank → retail global → renderer role** (the Ghidra pointer-assignment ladder
in `FUN_004984dc` decodes to this slot-ordered permutation):

| bank | global       | season-1d file | renderer bind          |
|------|--------------|----------------|------------------------|
| 0    | `DAT_00568208` | base1d.pl8   | terrain blob 0 (+ sky) |
| 1    | `DAT_0053e914` | mtns1d.pl8   | terrain blob 1         |
| 2    | `DAT_0057d344` | roads1d.pl8  | terrain blob 2         |
| 3    | `DAT_0055409c` | town1d.pl8   | terrain blob 3         |
| 4    | `DAT_0056898c` | castle1d.pl8 | terrain blob 4         |
| 5    | `DAT_00553224` | sprite1a.pl8 | feature `primary`      |
| 6    | `DAT_0056d8bc` | sprite1b.pl8 | feature `alt`          |
| 7    | `DAT_0055ce5c` | flags1a.pl8  | feature/overlay `overlay` |

Banks 5/6/7 are season-invariant (the table repeats sprite1a/sprite1b/flags1a
in every seasonal block).  Plus two trailing single loads: `misc_cty.pl8`
(size `DAT_0x4daec0` = 196000 → `DAT_005530c8`) and `vill_gd8.pl8` (fixed 0x12d8
read → `&DAT_00542ce0`).

**KEY: the captured strat-terrain1 frame is season 1d (winter), view 0**
(`DAT_0057c934 = 4`, offset 24) — not 1a as the binding-map table implied.
Proven by prefix-matching every captured blob to its `.pl8` file.

**DETERMINISTIC VALIDATION (no human needed):**
`tests/test_engine_pl8.c` drives the loader at season 4 / view 0 against the
real `.pl8` files (`$OPENLORDS2_GAME_DIR`) and byte-compares each loaded bank to
the captured retail in-memory blobs (`runs/capture/strat-terrain1/dump/`
terr_blob0..4 + blob_primary/alt/overlay).  **All 8 banks match over their full
file-content length** (128248 / 28670 / 147332 / 89040 / 121390 / 288296 /
62216 / 54261 bytes).  This proves the table (filenames + sizes), the
season/view offset arithmetic, and the bank→slot ladder all reproduce retail's
sprite data exactly.

A second test (`..._native_load_renders_identical_frame`) renders the composed
3-cluster scene (`engine_map_scene_render_body`) twice — once with the dump
blobs + the capture's heap over-read lengths, once with the native-loaded banks
+ their `bytes-read` lengths — and asserts the two 640×480 canvases are
**byte-identical** (drawA=263 drawB=29 sky=0 army=14 feat=291, matching the
scene harness).  This proves the loader→renderer integration end-to-end and that
the tighter content length drops no draws (the terrain blitter gates each copy
on `s + sz > blob_len`).

**Malloc-slack caveat:** the table size over-allocates the buffer by a few
dozen bytes vs the actual file (e.g. base1d table=128300, file=128248).  That
tail is never read by the blitters and is uninitialized in retail too (the
captured blobs show `0xcd` MSVC heap-fill there).  `engine_pl8_bank_size()`
therefore reports the bytes the read filled (the valid content length), not the
malloc ceiling — and that is the length to pass to `set_blob_lengths`.

**Deferred (faithful, not needed for the game-dir-cwd case):** the file-read
leaf's tiers 2/3 (CD-path / install-path `chdir` retries, `FUN_004af312` /
`FUN_004af116`) — only tier 1 is used (cwd = game dir).  The prelude aux-init
calls in `FUN_004984dc` (`FUN_004af502` / `FUN_004b25b0(220000)` / `FUN_0049915e`
/ `FUN_00499366` / `FUN_0049964c`, likely palette/arena setup) are not part of
the blob load and are not ported here.  `DAT_0053f050` (set =1 once data is
resident; the strat-map screen-dispatch arm gates on it) is mirrored as
`engine_pl8_data_ready()`.

**NEXT toward native:** wire screen-dispatch arm 0 (`FUN_0040f5fd`) to call
`engine_pl8_load_strategic_banks()` once at map entry, feed the bank pointers to
`engine_map_terrain_bind_blobs` / `engine_map_render_bind_blobs`, then call
`engine_map_scene_tick` — replacing the harness's captured-blob binds.  Still
gated on the HUD chrome fns (menu bar, right realm panel).

## Strategic-map menu bar (HUD chrome) — PORTED 2026-06-01

The top "File / Options / Help" bar is **engine-drawn into the 640×480 DDraw
back surface** (`DAT_004ea1a8`), NOT a Win32 menu — the binary calls `GetMenu`
only to size the window rect and never `SetMenu`/`LoadMenu`/`CreateMenu`.
`frida_capture.py` grabs that back surface, so the bar appears in the captured
truth frame; the map body is drawn into the same surface below it, which is why
truth and the port align row-for-row (the bar replaces the top ~18 rows, it does
not push the map down).

**`FUN_004b1867` is a CLEAR, not a chrome blit.**  It calls
`FUN_004b3e51(DAT_004ea1a8, 0x4b000)` = memset-to-zero of the whole 640×480
buffer (0x4b000 = 640×480), run every frame at the top of the strat-map arm
`FUN_0040f5fd`.  So the menu bar (and everything) is redrawn each frame, after
the clear, by the arm's draw calls.

**The bar is `FUN_00419c78`** (called by `FUN_0040f5fd` right after the map
body `FUN_004050c0`).  Composition:

| retail call                          | role                                   |
|--------------------------------------|----------------------------------------|
| `FUN_00409e09(0,0,0x19,1)`           | parchment strip, cols 0..24 (24×24 ea) |
| `FUN_00409e09(0x250,0,2,1)`          | parchment strip, cols 25..26 (right)   |
| `FUN_00403fdd(0,0,0x280,0x18)`       | 1-px bevel frame (gfx_line, ported)    |
| flag loop `FUN_0040a21a` ×realms     | per-realm shield (data-dependent)      |
| `FUN_0041a900(DAT_00553edc,0x168,6,3)`| year "1268" at x=360 (data-dependent) |
| `FUN_00402d37(0x1d,DAT_0057c934,…)`  | season "Winter" (data-dependent)       |
| `FUN_0041ab67(DAT_0057c018[r],0,500,…)`| gold "5000 Crowns." (data-dependent) |
| `FUN_0040c5b0(&DAT_004dc428,3)`      | File/Options/Help labels               |

**Parchment tiler `FUN_00409e09`** = a 24×24 sibling of the 16×16 panel tilers
(`FUN_0040946d`/`004097c5`, ported in gfx_panel.c).  Per tile it stages a
descriptor from the chrome sheet directory `chrome + 8 + idx*0x10` (sprite index
`0xc4 + (col & 7)`, cycling 8 parchment tiles `0xc4..0xcb`) into the shared
gfx_glyph/gfx_dispatch state and dispatches the already-ported fixed 24×24 raw
blit `gfx_sprite_blit_24x24_raw` (`FUN_004b4cb8`).  The chrome sheet is the
`0x57d3d0` blob = **panels.pl8**, loaded into a fixed buffer by `FUN_00499859`
entry 10 (the port loads it as `gfx_load_chrome_sheet()`; the title screen
already binds it).

**Labels `FUN_0040c5b0`** walk `DAT_004dc428` (three int16_t[8] records: x=10,
y=6, loc-id 1/2/3 = File/Options/Help).  Layout is a running x cursor seeded to
the first record's x; each label draws at the current cursor via
`FUN_00402d37` (gfx_wrap_localized_line over the l2.eng blob + menu font
`DAT_005af8f0`), and the renderer's `line_x_acc` (`DAT_005cd404`) advances by the
drawn width, then `+0x20` spacing is added.  This yields File@10, Options@~95,
Help@~190.  The hovered item (`DAT_00522cb4`, 1-based) gets a highlight box
(`FUN_004b414a`) sized from the measured width and renders in color 0x18.

**Port:** `src/engine_hud_menubar.{c,h}` ports `FUN_00409e09`
(`engine_hud_menubar_tile_strip`) and `FUN_0040c5b0`
(`engine_hud_menubar_labels`) — both fully — plus the state-free composition
`engine_hud_menubar_render_chrome` (the two strips + bevel + labels).  Wired into
the `--strat-map` bring-up shim (main.c) after the map body.

**Validation (live --strat-map vs strat-spray1 frame_00009):** the menu-bar
region jumped from ~all-wrong to a clean match — **the left 270 px (parchment,
bevel, File/Options/Help) is PIXEL-PERFECT (zero diff px)**.  Every residual
pixel sits at x≥270, exactly the **deferred** data-dependent tail of
`FUN_00419c78`: the realm shield (~x270), year "1268" (~x365-401), season
"Winter" (~x414-473) and gold "5000 Crowns." (~x504-616).  Those read per-realm
state (`DAT_0057bf*`/`DAT_0057c018`/`DAT_00553edc`, 0x160 stride) the live
new-game/load path does not yet populate — `FUN_0041a900`/`FUN_0041ab67` and the
flag loop are left unported (cited as bare hex) until that state lands.  5 host
tests (tile cycling/origin/abort/null + table layout); File/Options/Help label
rendering is validated by the render-parity harness, not re-stubbed.

## Strategic-map menu bar — REALM-STATUS TAIL PORTED 2026-06-01

The data-dependent tail of `FUN_00419c78` (deferred above for lack of game
state) is now ported and **rendered from the natively-loaded save** in
`--strat-load`.  The save-load path populates exactly the globals it reads:
`g_player_strategic_352` (entry 3, the 6×0x160 per-player block), `s_slot_00553edc`
(year, entry 20), `s_slot_0057c934` (season, entry 23), `s_slot_0057c8cc`
(current realm, entry 72), `s_slot_0057c8fc` (HUD gate, entry 14).

**Port** = `engine_hud_menubar_render_status` (engine_hud_menubar.c), composing
already-ported leaves — no new blitter/text primitives were needed:

| retail piece                         | port leaf used                              |
|--------------------------------------|---------------------------------------------|
| flag draw `FUN_0040a21a`             | `gfx_dispatch_sprite` (= FUN_00404442)      |
| year/gold number `FUN_0041a900` m3 / `FUN_0041ab67` | `gfx_text_label_render_dec` (FUN_00402f64) |
| season + " Crowns." `FUN_00402d37`   | `gfx_wrap_localized_line`                   |

Key facts pinned during the port:
- **`FUN_0040a21a` ≡ `gfx_dispatch_sprite` (FUN_00404442) + bounds-check
  logging.**  The success arm of 40a21a is byte-identical to 404442 (same
  descriptor staging, clip setup `FUN_0040464c/477b`, source-offset adjust
  `+= width*DAT_0058feac + DAT_0058fd50`, then `FUN_004b446d/44d2/43b1`); the
  error arms only fire on a NULL sheet / out-of-range offset, which a valid
  misc_cty.pl8 + sprite index never trips.  Flag sprite = `realm[+0x0a] + 0x55`
  off the misc_cty sheet (`DAT_005530c8` → `engine_pl8_misc_cty()`).
- **Flag-loop filter:** realms 1..5, drawn when `realm[+0x04] != 0` (active) &&
  `*(int*)realm[+0x00] < 999`, packed L→R from x=0x10e (270), y=4.
- **Format strings `DAT_004d41d4..f0` are all the single space `" "`**
  (r2-dumped); the gold number suffix `DAT_004d41f4` is empty — " Crowns."
  comes from loc group 8 (sub 0 "Crown." for ±1, sub 1 "Crowns." otherwise).
- **`line_x_acc` (`DAT_005cd404`) is a relative advance accumulator** —
  `gfx_text_render_line` draws at the passed `cursor_x` but adds each glyph's
  width to `line_x_acc` (plus a trailing `+4`).  41a900/41ab67 zero it, render
  the number, then place the following string at `base + line_x_acc`.  Gold
  field = `realm[current_realm].+0x118` (retail `DAT_0057c018`).

**Validation — PIXEL-PERFECT (0 px) vs the immediate-load retail frame.**
`--strat-load` logs `status — realm=1 gate=0 year=1268 season=4 gold=1000`,
matching retail byte-for-byte, and the rendered status region (x260-640, y0-24:
5 flag shields red/gold/black/magenta/blue + "1268" + "Winter" + "1000 Crowns.")
diffs **0/9120 px** against `runs/capture/load_clean_dump/frames/frame_00000.bmp`.

**GROUND-TRUTH NOTE — use an IMMEDIATE-load frame, not the deep warmup.**  The
flag *count* is a timing tell, the same warmup caveat as the +7 region→owner
remap: an immediate-load capture (`load_clean_dump`, `load_at_30`) shows all
**5** realm shields (the 5-realm game at turn start), whereas the deeper-warmup
`load_clean_32` (~32 s of turbo) has collapsed to **1** shield even though its
year/gold are unchanged.  The port renders the immediate-load state, so validate
against `load_clean_dump` (5 shields), not `load_clean_32` (1).  The retail
`.pal` files are NOT interchangeable for the shields — palette-applying a
mismatched capture's `.pal` to the port's 8bpp raw recolours the shields
(blue↔red etc.); compare the port's own presented `port_strat.bmp` against the
matching capture instead.  The "5000 Crowns" in the older menu-bar note was a
new-game spray value; `lastturn.sav` is 1000 Crowns / year 1268 / Winter.

## Native data path (load route) — FOUNDATION + the descriptor-finalize gap (2026-06-01)

Goal: make the drop-in produce the strategic-map render arrays itself
(load a save + build), retiring the `--strat-map` shim's captured-dump
seeds.  Route chosen: **load** (`save_load` is already ported + tested vs
the real `lastturn.sav`), not new-game map-gen.

**Ported + validated (`src/engine_map_init.{c,h}`, `--strat-load DIR`):**
- `FUN_004298c1` + `FUN_0046797d` → `engine_map_init_build_cells`: builds
  `tile_cells` (0x55cea0) = init 0xfff0000, seed terrain from
  `L2_maps.dat[map_id]+0x6000`, then the iso territory-index projection
  `(cols*row+col)*stride`.  **VALIDATED**: the projection matches the
  captured `tile_cells.bin` 4093/4093 playable cells (off980=0).
- `FUN_00451fcc` → `engine_map_init_set_camera`: view-mode-0 camera
  (matches the shim's hardcoded scalars).
- `FUN_00498270` → `engine_map_init_strategic`: 64×64 dims, stride 8,
  default scroll origin (col 20 / row 74 — the pre-scroll view).
- `bringup_strat_load` (main.c): `save_load(lastturn.sav)` →
  `engine_map_init_strategic` → bind clusters to the native arrays →
  render.  Byte-exact self-check vs the dumps built in.

**The remaining gap — the in-place terrain FINALIZE pass (`FUN_004676e0`).**
The save stores the *logical* map at 0x522f90; a map-entry pass rewrites it
**in place** into display descriptors.  Field-by-field, raw save entry 0 vs
the captured (post-finalize) `descriptors.bin`:

| field | meaning            | save→dump match |
|------:|--------------------|----------------:|
|  +3   | terrain sprite idx |   **4.0%**      |
|  +1   | edge/flags         |   42.4%         |
|  +7   | terrain flag bits  |   36.6%         |
|  +2   | mode gate flag     |   62.6%         |
|  +5   | **feature head**   |   **99.8%**     |
|  +0/+4/+6 | (other)        |   92–97%        |

So the **features/territory load correctly** (head +5 = 99.8%, territory
records = 99.84%) — the map is NOT regenerated — but the **terrain display
sprite (+3) is ~96% recomputed** by the un-ported finalize, so the terrain
surface renders wrong and dominates the frame (17.6% vs strat-spray1).

`FUN_004676e0` (144 B) is an orchestrator of **15 sub-passes**: 6×
`FUN_0046c5b8(type)` (per-terrain autotiling), `FUN_00467770`/`0046a037`
(map-id keyed), `FUN_0046df51`/`0046dbf5`/`0046dd34` (tile_cells edge
variants — note: these only touch the *inactive* 0xfff cells; the playable
projection already matches), `FUN_00467ca6`/`00468d4f`/`00467a36`
(settlement sprites) / `0046da4b` / `004291b3`.  Porting this cluster is
the NEXT frontier for the native data path; it makes `--strat-load`
reproduce strat-spray1 from loaded data and is the same pass new-game would
need.  `engine_map_init_strategic` leaves a `TODO` seam where it runs.

### CORRECTION (2026-06-01): the finalize is already ported — the gap is a load-path OWNER-STAMP + RNG phase

A host diagnostic (`tests/test_map_finalize.c`,
`test_map_finalize_load_path_diagnostic`; SKIPs without the retail dir +
`runs/capture/strat-terrain1/dump`) loads the real `lastturn.sav`, runs the
ported load-finalize, and measures the per-field descriptor match vs the
captured `descriptors.bin`.  Findings overturn the framing above:

- **`FUN_004676e0` and every sub-pass are ALREADY ported** in
  `engine_territory.c` — the autotiler `FUN_0046dbf5`
  (`engine_territory_classify_feature_tiles`), `46dd34`, `46da4b`,
  `46c5b8`, `467770`, etc.  The earlier "task #11 = port the finalize" was
  stale.
- **The LOAD path is `FUN_004adfe6` (the save-load core = ported `save_load`)
  → `FUN_004976a1`** (map setup): `4298c1` build_cells → `451fcc` camera →
  `46a037` overlay (deferred) → **`46dbf5` autotile → `46dd34` randomise →
  `46da4b(0)` roster**.  It does NOT run the new-game `46c5b8` clears or the
  `467770` plane-unpack, so the loaded `+0/+5/+6` bytes survive (matches the
  +5 = 99.8% observation).
- **The autotiler port is byte-faithful** — r2-verified end to end: the two
  hex neighbour-offset tables (`0x4d6c28` even / `0x4d6be8` odd) match the
  hardcoded `NEIGHBOUR_OFFSETS`; eligibility `46de94` = `(tile<0xfff0000 &&
  (desc[+1]&4)==0) || tile==0xfff0006`; the 48-entry rule table `0x4d7310`;
  the match loop `46c2de`; and the build seeding `46797d` (= `seed_terrain_layer`,
  inits inactive cells to `0xfff0000+terrain`).  Given identical input it
  produces retail's classification.
- **THE ACTUAL GAP — a load-time spatial OWNER-STAMP of cell +7.**  Per-field
  match of the raw save vs the captured frame: `+5 = 99.8%`, `+0 = 91.7%`,
  `+4 = 93.4%` (preserved) but `+1 = 42%`, `+2 = 63%`, `+3 = 4%`, `+7 = 37%`
  (recomputed).  The render only writes `+2`/`+3` (not `+1`/`+7`), so `+1`/`+7`
  are recomputed by a NON-render load pass.  Histograms:
  - **save `+7` ∈ {0, 1..0x0e, 0x20}** (per-cell *region* id, ~115 cells each)
  - **captured `+7` ∈ {0, 1, 2, 3, 4, 0x20}** (owning *realm*; sea `0x20`
    grows 84 → 497)
  - `+1 bit2 == (+7==0)` holds **4096/4096 in BOTH** — the border gate just
    tracks `+7`.
  So load **remaps `+7` from region-id → owning-realm**.  The mapping is
  **many-to-many / spatial** (save `+7=0x06` → owners 0/3/4), NOT a
  per-territory-id table (owner = record byte +0x00 scores only 21%) — i.e. a
  spatial ownership-fill, not yet identified/ported.  The autotile reads the
  *post-stamp* `+1`/`+7`, so until the stamp runs, `+3` can't match.
- **Consistency confirmed:** the save's loaded `territory_records` match the
  captured `territory_records.bin` at **99.8%**, and `+5` at 99.8% — the
  on-disk save and the capture are the **same game state** (a trustworthy
  pair).  So byte-exact reproduction IS achievable.
- **Water `+3` is RNG** (single-LFSR `DAT_00591564`, seeded `0x34518632` at
  InitInstance): autotile type `0x16` → variant `rng&7`.  Byte-exact needs
  RNG-phase parity — replay the consumers in order from the seed, or snapshot
  `DAT_00591564` at finalize entry via frida.

**NEXT (structural parity):** capture retail's `save_load → map-frame` call
trace (Phase-G `call_trace_diff` / frida, host run) to enumerate exactly which
functions run, find the `+7` ownership-fill pass, port it, then close the RNG
phase.  The diagnostic test guards the foundation invariants (save+capture
consistency, build correctness) and prints the live gap metrics each run.

### RESOLVED (2026-06-01) — the LOAD path is byte-exact; the gap was a wrong ground truth

Drove the structural-parity tools myself over remote frida
(`cutestation.soy:27042`):

- **Real load path** (call-trace, `--input-segtrace load_into_map_seg.jsonl`):
  `4ad9f0` (load pump, polled 351×) → **`4adfe6` (save_load)** → **`4976a1`**
  (map setup: `43e9e2`, `4298c1`, `451fcc`, `46a037`, **`46dbf5` autotile,
  `46dd34`, `46da4b`**) at frame 566 → `MAP_FREEROAM`@567.  (My earlier
  `--input-trace` runs never completed the load — the frame-3
  `497a34`/`467770` they saw was just the BOOT default map.  Use
  `--input-segtrace` with the anchor `{wait}` gating.)
- **Immediate post-load dump:** `--dump-regions 0x522f90:32768` at
  `MAP_FREEROAM+2` (warmup≈0) → `runs/capture/load-immediate1/dump/`.  The
  ported load (`save_load` + `classify_feature_tiles`/`randomise_special_tiles`/
  `rebuild_cell_roster`) reproduces it **+0/+1/+3/+4/+5/+7 = 100%, +2 = 99.5%**.
  THE LOAD DATA PATH IS CORRECT.
- **The "4% +3" was a wrong ground truth.**  `strat-terrain1` was captured ~32 s
  into the frame-capture WARMUP.  By then a turn/territory pass has remapped
  cell `+7` from its per-cell REGION id (`{1..0x0e}`, what the load produces) to
  the OWNING REALM (`{1..4}`; sea `0x20` 84→497) and recomputed the terrain
  display.  Immediate post-load `+7` = `{0:2337, 1..14:~115, 32:84}`;
  warmup `+7` = `{0,1,2,3,4,32}`.

**NEXT:** (a) wire the load finalize into the drop-in `--strat-load` so it
renders the immediate-post-load map natively; (b) find the WARMUP region→owner
remap (call-trace frames 567→~3700) to reproduce the settled `strat-terrain1`
frame; (c) chase the +2 0.5% residual (~20 cells).  `test_map_finalize_load_path_diagnostic`
now asserts the byte-exact load result.
