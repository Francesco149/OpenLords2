# Combat-mode (battlefield) renderer — survey

The battlefield is the screen rendered when `DAT_0057c8fc == 2`
(game-mode = combat) and `DAT_004eac50 ∈ {0x28..0x2a}` (screen-state
= one of the three combat screens).  It is a hex-grid scene drawn
by a cluster of seven pass functions hung off a single frame driver
at `FUN_004bc142`, plus a sizeable family of helpers (hit-test,
screen↔hex coord conversion, scroll, pan).

This is the combat-mode analog of the strategic-mode renderer
cluster (`FUN_004050f6` + 3 passes, all in `0x405000..0x408000`),
but lives in a different address band (`0x4bc000..0x4bf300`) and
uses a different tile format and storage layout.

**Status**: chips 1, 2, 3, 3.5, 4, 5a.i, 5a.ii.a, 5a.ii.b, 5a.iii,
5a.iv, 5b, 5c, 6 ported (sprite primitives + frame driver + both zoom-
dispatched terrain passes + zoom-32 overlay + middle-pass driver
shell + sorted-entity infrastructure + per-entity draw base path +
three per-cell draw leaves + three type-special leaves + late-pass
driver + per-cell creature draw + selection marker + coord helpers).
Final pass still stubbed.  Survey landed 2026-05-25, chips 1–4 + 5b +
6 + 5a.i + 5a.ii.a + 5a.ii.b + 5a.iii + 5a.iv + 5c landed 2026-05-25.

## Entry point

```
FUN_004b99c0 (main-loop tick, line 88581)
  └── FUN_004bc142(DAT_0055ce70, DAT_005678a0)   ← combat frame driver
        ├── FUN_004bc8fc   ← zoom-16 first pass    (terrain, 725 B)
        ├── FUN_004bcbdc   ← zoom-32 first pass    (terrain, 1187 B)
        ├── FUN_004bd355   ← middle pass           (zoom-indep, 543 B)
        ├── FUN_004bcbd1   ← zoom-16 second pass   (stub! 11 B)
        ├── FUN_004bd07f   ← zoom-32 second pass   (overlay,  436 B)
        ├── FUN_004bd233   ← late pass             (290 B)
        └── FUN_004bf1f9   ← final pass            (433 B)
```

The driver is gated by the camera-coord setter at line 88581 inside
the `DAT_0057c8fc == 2` branch of the main-loop dispatcher.  Camera
coords come from `DAT_0055ce70` / `DAT_005678a0` (same globals the
strategic primary pass uses — likely shared single-camera state).

Zoom-level dispatch (`DAT_004e656c`):

  - `0x10` (16 px/hex)  → `FUN_004bc8fc` then `FUN_004bcbd1` (stub)
  - `0x20` (32 px/hex)  → `FUN_004bcbdc` then `FUN_004bd07f`

The middle/late/final passes (`FUN_004bd355`, `FUN_004bd233`,
`FUN_004bf1f9`) run unconditionally regardless of zoom.

End of frame: `DAT_0053e9ac` (force-redraw counter) is decremented —
identical pattern to `FUN_004050f6` line 2599-2601.  When non-zero
it overrides the per-cell dirty bit, forcing every cell to redraw.

## Cluster — full file-static helper set

Functions in `0x4bc000..0x4bf300`, in address order:

| addr        | role                                            | size  |
|-------------|-------------------------------------------------|------:|
| `0x4bc020`  | unsurveyed                                      | 231 B |
| `0x4bc107`  | unsurveyed                                      |  59 B |
| `0x4bc142`  | **frame driver (entry)**                        | 143 B |
| `0x4bc1d1`  | horizontal scroll/pan helper                    |  81 B |
| `0x4bc222`  | hit-test (point-in-grid)                        | 131 B |
| `0x4bc2a5`  | screen-x → hex-x                                |  32 B |
| `0x4bc2c5`  | screen-y → hex-y                                |  32 B |
| `0x4bc2e5`  | variant screen→hex (offset)                     |  97 B |
| `0x4bc346`  | variant screen→hex (offset)                     | 102 B |
| `0x4bc3ac`  | unsurveyed                                      |  97 B |
| `0x4bc40d`  | unsurveyed                                      | 100 B |
| `0x4bc471`  | unsurveyed                                      |  74 B |
| `0x4bc4bb`  | unsurveyed                                      |  74 B |
| `0x4bc505`  | unsurveyed                                      |  21 B |
| `0x4bc51a`  | scroll-edge handler? (FUN_004bc1d1 callee)      | 994 B |
| `0x4bc8fc`  | **zoom-16 first pass** (terrain blit)           | 725 B |
| `0x4bcbd1`  | zoom-16 second pass (no-op stub)                |  11 B |
| `0x4bcbdc`  | **zoom-32 first pass** (terrain blit)           |1187 B |
| `0x4bd07f`  | **zoom-32 second pass** (overlay)               | 436 B |
| `0x4bd233`  | **late pass**                                   | 290 B |
| `0x4bd355`  | **middle pass** (zoom-independent)              | 543 B |
| `0x4bd574`  | unsurveyed                                      | 485 B |
| `0x4bd759`  | unsurveyed                                      | 479 B |
| `0x4bd938`  | unsurveyed                                      | 346 B |
| `0x4bda92`  | unsurveyed                                      | 259 B |
| `0x4bdb95`  | unsurveyed                                      | 156 B |
| `0x4bdc31`  | unsurveyed                                      |1087 B |
| `0x4be070`  | unsurveyed                                      | 719 B |
| `0x4be33f`  | unsurveyed                                      | 416 B |
| `0x4be4df`  | unsurveyed                                      | 735 B |
| `0x4be7be`  | unsurveyed                                      | 763 B |
| `0x4beab9`  | unsurveyed                                      |1051 B |
| `0x4beed4`  | unsurveyed                                      | 805 B |
| `0x4bf1f9`  | **final pass**                                  | 433 B |

Total: ~12 KB of decompilation across ~34 functions.

## Sprite primitive — `FUN_004b49f7`

The terrain pass at `0x4bc8fc` (and its zoom-32 sibling at `0x4bcbdc`)
calls a NEW sprite-render primitive `FUN_004b49f7` @ 0x4b49f7 (705 B)
that the project hasn't ported yet.  Unlike the strategic-mode
renderers, which use the RLE family in `gfx_sprite_rle.c`, this is a
**raw 16×16 4-byte-per-pixel block copy**:

```c
puVar2[0]     = puVar1[0];   // row 0
puVar2[1]     = puVar1[1];
...
puVar2[0xa0]  = puVar1[4];   // row 1 — stride 0xa0 dwords (= 0x280 B
                             //          = screen pitch / 4)
...
puVar2[0x963] = puVar1[0x3f];// row 15
```

16 rows × 4 dwords per row = 64 dwords (= 256 bytes) per tile, copied
into a screen buffer at `DAT_004ea1a8 + DAT_00591524 + DAT_00591528 *
DAT_004eb274`.  The destination stride is `DAT_004eb274` (screen
pitch); the source stride is implicit `0x10` (each row starts at
`puVar1 + 0x10`).

Five non-battlefield callers also use this primitive (visible at
lines 4712, 4738, 4934, 5086, 6705 of the decompilation), so
porting it unblocks more than just the battlefield path.

## Storage layout

The combat-mode tile array is REFERRED TO BY THE PASSES VIA GLOBALS
that the survey hasn't deeply analyzed yet.  Initial reading of
`FUN_004bc8fc` (the simplest pass — see below) yields:

  - `DAT_004e6574` — base pointer of the per-hex descriptor table.
                    Each entry contains at least bytes at +2 (flags)
                    and +3 (sprite index).  Stride is `DAT_004e5d50`.
  - `DAT_004e6588` — running cursor into that table (re-set each
                    frame from `DAT_004e6578 * DAT_004e6580 + DAT_004e657c`
                    × `DAT_004e5d50` — i.e. camera Y × row-stride
                    + camera X, times tile-stride).
  - `DAT_004e5d5c × DAT_004e5d4c` — visible hex grid dimensions
                    (horizontal × vertical), in cells.
  - `DAT_004e6558` / `DAT_004e6554` — screen pixel offset for the
                    visible area's top-left corner.
  - `DAT_004e656c` — zoom level (16 or 32).
  - `DAT_004e5d70` — row-stride adjustment after a complete row of
                    hexes (probably the wrap-around to the next row).
  - `DAT_004e5d78` / `DAT_004e6560` — two sprite descriptor blobs.
                    The per-cell flags byte selects between them via
                    `(flags & 0x1c)`:  0 → primary blob, 4 → secondary.

The per-cell flags byte (+2 of descriptor) carries:

  - bit `0x01` — dirty (cell needs redraw); cleared after draw
  - bit `0x02` — boundary cell (set after draw for first/last row/col)
  - bits `0x04..0x10` — sprite-blob selector (0 or 4 in `FUN_004bc8fc`)
  - bits `0x20..0x80` — unsurveyed (other passes likely consume them)

The dirty-bit ANDs with the redraw decision:

```c
if ((DAT_0053e9ac != 0) || ((bVar1 & 1) != 0)) {
    *(byte *)(descriptor + 2) &= 0xfe;  // clear dirty
    ... stage + dispatch sprite ...
}
```

— i.e. force-redraw counter overrides per-cell dirty.

Boundary-cell marking writes the `0x02` bit AFTER the draw so that
next frame those cells are re-drawn even when nothing else changed
— this is the "edge re-paint" trick that handles scroll smear.

## Sprite metadata staging

The terrain pass stages the same 6-field sprite metadata struct that
the strategic-mode RLE renderer uses, into the SAME retail globals:

  - `DAT_005c9258`  — width    (puVar2[0],  16-bit)
  - `DAT_005aeb70`  — height   (puVar2[1],  16-bit)
  - `DAT_0058fe04`  — src offset (puVar2[2..3], 32-bit)
  - `DAT_00591568`  — flag byte (puVar2[6])
  - `DAT_005bb478`  — depth/flag2 (puVar2[6.5], byte at +0xd)
  - `DAT_0057d3c0`  — flag3   (puVar2[7])

So the sprite metadata format is shared with the strategic-mode
sprite_render family — only the actual draw primitive
(`FUN_004b49f7` vs the RLE renderers) differs.

## What's next

The cluster is too large to port in one chip.  Suggested chip plan:

1. **`FUN_004b49f7`** (705 B raw 16×16 blit primitive) — chip 1.
   **LANDED** (2026-05-25) as `gfx_sprite_blit_16x16_raw`.

2. **`FUN_004bc142`** (143 B driver) + zoom-16 stub `FUN_004bcbd1`
   (11 B) — chip 2.  **LANDED** (2026-05-25) as
   `engine_battlefield_render` module.  The driver is fully ported;
   the zoom-16 second pass (a retail no-op) is ported faithfully;
   the other five passes are wired as static call-counter stubs
   that get swapped for real ports in chips 3–5.

3. **`FUN_004bc8fc`** (725 B zoom-16 first pass) — chip 3.
   **LANDED** (2026-05-25) as `pass_terrain_zoom16` inside
   `engine_battlefield_render`.  Adds bindings for the descriptor
   table, grid dimensions, viewport offset, camera row stride, and
   primary/alt sprite blobs.  Stages cursor + sprite metadata via
   the shared `gfx_dispatch_stage_sprite_descriptor`, then calls
   `gfx_sprite_blit_16x16_raw`.  Boundary cells get bit 0x02
   (re-paint on next frame).

4. **`FUN_004bcbdc`** (1187 B zoom-32 first pass) — chip 4.
   **LANDED** (2026-05-25) as `pass_terrain_zoom32` inside
   `engine_battlefield_render`.  Two new per-cell behaviors over
   the zoom-16 sibling: (a) `descriptor[+1] & 4` EARLY-SKIP gate
   that drops dirty cells without drawing OR boundary marking, and
   (b) a SECOND clipped sprite per cell (gated on `descriptor[+0]
   != 0` and `descriptor[+4] ∈ {1, 2, 3}`) that always pulls from
   the alt blob, picks slot `descriptor[+0] + 0x8b` (clamped at
   `0x9a` when `descriptor[+0] >= 0x10`), and goes through the
   full clipped-blit dispatcher (clip_x → clip_y →
   apply_skip_and_dispatch_sprite).  Both the first and second
   sprite share a `[1, 310000]` glyph_offset gate that aborts the
   cell entirely on out-of-band offsets (skips boundary marking +
   skips the second sprite).  Adds one new binding:
   `bind_clip_rect(left, top, right, bottom)`.  The 32×32 raw blit
   primitive (`gfx_sprite_blit_32x32_raw` — chip 3.5) is the
   target for the first sprite; the existing clipped sprite blit
   family (`gfx_sprite.c`) is reused for the second sprite via
   `gfx_dispatch_apply_skip_and_dispatch_sprite`.

5. **`FUN_004bd355`** + **`FUN_004bd233`** + **`FUN_004bf1f9`** —
   chips 5a/c/d.  Middle, late, final passes — units/effects on
   top of terrain.  The middle-pass cluster is much larger than the
   543 B headline (the survey turned up ~6700 B across 11 functions
   including its leaves and per-entity helpers — see the chip 5a sub-
   plan below).  Late + final remain unsurveyed stubs.

   **`FUN_004bd355`** — chip 5a (split into sub-chips).
   The middle pass calls three setup helpers at the head
   (`FUN_004bd938 / FUN_004bda92 / FUN_004bdb95` — build / sort /
   render a Y-ordered list of visible entities) and two per-cell
   leaves (`FUN_004bd574` player-marker draw, `FUN_004bd759` unit-
   key draw, gated on `descriptor[+2] & 0x80`) plus an effect-chain
   walk (`FUN_004beed4`, gated on `descriptor[+6] != 0`).  The
   sorted-entity render walks into another 1087 B per-entity
   renderer (`FUN_004bdc31`) that fans out to three type-specials
   (`FUN_004be4df` 735 B / `FUN_004be7be` 763 B / `FUN_004beab9`
   1051 B).

   Sub-chip plan:

     - **chip 5a.i** — middle-pass driver shell only.  Real port
       of `FUN_004bd355` (the loop, the over-scan extension, the
       per-cell dispatch shape), with all six leaves as counter-
       stubs.  Adds one new binding: `bind_total_rows`
       (DAT_004e6570).  Filed quirks #83 (over-scan extension) and
       #84 (asymmetric re-dirty).  **LANDED** (2026-05-25).
     - **chip 5a.ii.a** — sorted-entity infrastructure (no per-entity
       draw body).  Real ports of `FUN_004bd938` (visible-list build,
       346 B), `FUN_004bda92` (bubble sort with 500-iter safety, 259 B),
       and `FUN_004bdb95` (per-entity render loop, 156 B).  The
       per-entity draw `FUN_004bdc31` is wired as a state-capturing
       stub that records (id, px_x, px_y) per call so tests can
       verify the render loop's cursor computation.  Adds new
       binding `bind_entity_table` plus visible-list, sort-passes,
       and per-entity-stub accessors.  Filed quirks #85 (inclusive
       filter bounds) and #86 (in-band sentinel + stable-ish
       bubble sort).  **LANDED** (2026-05-25).
     - **chip 5a.ii.b** — per-entity draw body.  Real port of
       `FUN_004bdc31` (1087 B base path) with the three type-
       specials (`FUN_004be4df` / `FUN_004be7be` / `FUN_004beab9`,
       chip 5a.iv) still stubbed.  Adds new bindings for the
       anim-offset tables (`DAT_004e3bf0` zoom-16, `DAT_004e4030`
       zoom-32 — each 0x440 bytes, 8 facings × 17 frames per facing)
       and the dirty-rect state pointer.  Filed quirks #87 (type-6
       PRE-call survives no-blob path), #88 (uninitialised locals
       at unsupported zoom), #89 (dirty box sized by entity type
       not sprite dims).  **LANDED** (2026-05-25).
     - **chip 5a.iii** — per-cell draw leaves.  Real ports of
       `FUN_004bd574` (player marker, 485 B — animated through 8
       frames, slot from per-player banner-band table), `FUN_004bd759`
       (unit-key sprite, 479 B — 4-byte map {0x49→0x1f, 0x4c→0x20,
       0x61→0x21, 0x64→0x22} silently drops unknown keys),
       `FUN_004beed4` (effect-chain walk, 805 B — max 10 hops, self-
       loop guard, animated subtype-5 effects with their own 0..15
       wrapped frame counter and 16-entry jitter table, subtype-7
       skips render but continues walk).  New bindings:
       `bind_player_marker_blob / slot_table / player_id / anim_advance`,
       `bind_unit_key_blob`, `bind_effect_chain_table / shared_blob /
       anim_table`, `bind_scroll_pad`.  Filed quirks #90 (effect-chain
       glyph_offset gate aborts whole chain), #91 (player_marker +
       unit_key Y-axis re-centering uses sprite WIDTH not height),
       #92 (effect-chain dirty-box branch identical in both sides).
       **LANDED** (2026-05-25).

     - **chip 5a.iv** — type-special leaves consumed by per-entity
       draw.  Real ports of `FUN_004be4df` (type-6 PRE-call, 735 B —
       shadow/ground effect drawn beneath the main sprite, uses a
       separate "secondary" blob bound via the new
       `bind_entity_type6_blob`), `FUN_004be7be` (type-7 POST-call,
       763 B — facing-selected blob pair via
       `bind_entity_type7_blobs(low, high)`, switch at facing < 4 /
       >= 4), `FUN_004beab9` (type-9-subtype-0xe POST-call, 1051 B —
       draws TWO sprites per call from the entity's primary blob,
       slots `secondary_frame + 0x16` and `secondary_frame + 0xd`,
       anchored at the entry cursor with Y biases -16 and +0x56
       respectively).  All three use a 6×6 tile dirty box (vs the
       per-entity main draw's 5×5/9×9 by entity type, and the
       per-cell leaves' 4×4).  Filed quirks #93 (type-9-sub-0xe
       reads z32 anim table at zoom 16 too), #94 (type-9-sub-0xe
       second sprite anchored at SAVED entry cursor, not first
       sprite's adjusted cursor).  **LANDED** (2026-05-25).

   All four sub-helpers consumed by the chip-5a draw leaves —
   `FUN_004527a6` (cell-tiled dirty-rect mark), `FUN_004b4537`
   (unclipped RLE blit), `FUN_004b4814` (left-clipped RLE), and
   `FUN_004b48b8` (right-clipped RLE) — are ALREADY PORTED in the
   project (`engine_dirty_rect_mark_box_tiled`, `gfx_sprite_rle_*`,
   dispatched via `gfx_dispatch_dispatch_sprite_rle`).

   **`FUN_004bd07f`** — chip 5b.  **LANDED** (2026-05-25) as
   `pass_overlay_zoom32` inside `engine_battlefield_render`.  The
   second-layer pass that complements terrain-z32: drawing
   requires descriptor[+1] & 4 SET (the same bit terrain-z32
   uses as its EARLY-SKIP gate, but the opposite polarity).  Net
   effect: per cell, at most one of terrain-z32 or overlay-z32
   draws per frame, picked by the layer bit.  Always uses primary
   blob; no boundary marking; no second sprite.  Same [1, 0x4baf0]
   glyph_offset gate as terrain-z32.  Filed quirks #79, #80, #81.

   **`FUN_004bd233` + `FUN_004be070` + `FUN_004be33f`** — chip 5c.
   **LANDED** (2026-05-25) as `pass_late` + `late_leaf_draw_creature`
   + `late_leaf_draw_selection_marker` inside
   `engine_battlefield_render`.  The late pass walks visible cells
   (NO over-scan, unlike the middle pass), gates each cell on
   `descriptor[+5] != 0` AND the AND of two visibility-mask bytes
   (per-entity at entity record offset +0x0a, per-player at the
   bound mask-table base).  For each visible cell, dispatches the
   per-cell creature draw leaf (`FUN_004be070`, 719 B):
   stages a sprite descriptor from the creature blob with slot
   derived from per-entity +0x1a0 short (shifted by the bound shift
   amount, plus 0x2e), applies a type-conditional Y offset
   (<7 / ==10 / else / +0x10 if ==9), and dispatches the RLE blit
   through the same clipped family the chip 5a leaves use.  After
   the cell loop, if the selection-marker gate is positive, the
   second leaf (`FUN_004be33f`, 416 B) draws a fixed-slot (0x3f)
   sprite at the active hero/army's world position, marks a 3×3
   tile dirty box, and invokes a world-map dirty hook (bound as a
   callback — retail's `FUN_0046c4a3` is intentionally NOT pulled
   into the battlefield module).  Adds new bindings:
   `bind_creature_sprite_blob` (retail DAT_005530c8 — misc_ske.PL8),
   `bind_late_player_visibility_mask(base, stride, player_id)`,
   `bind_creature_slot_shift(amount, sign)`,
   `bind_selection_marker(x, y, gate)`, and
   `bind_world_map_dirty_hook(hook)`.  Filed quirks #95 (creature
   draw reads z32 anim table at zoom 16 too), #96 (lopsided cursor
   restore on gate fail), #97 (per-cell pitch HARDCODED 0x20
   regardless of zoom).

6. Helpers — interleaved as required:
   - `FUN_004bc222/2a5/2c5/2e5/346` (hit-test, screen↔hex).
     **LANDED** (2026-05-25) as the coordinate-helper family at
     the end of `engine_battlefield_render.{c,h}`.  Five tiny
     pure functions (32–131 B each, 394 B total) sharing the
     module statics already bound by the render passes (plus
     one new binding `bind_cell_band_width` for retail
     DAT_004e5d3c).  Filed quirk #82 (left-biased asymmetric
     clamp).
   - `FUN_004bc1d1/51a` (scroll/pan) — possibly large state
     machine for keyboard/mouse panning.

Total cluster: ~12 KB across ~34 functions.  Comparable in
scope to strategic-mode rendering, which took 4 chips.

> 📍 Ghidra `FUN_004bc142` (entry), `FUN_004b49f7` (sprite
> primitive), `FUN_004b99c0` line 88581 (caller).
