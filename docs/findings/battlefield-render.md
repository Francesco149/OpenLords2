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

**Status**: chips 1, 2, 3 ported (sprite primitive + frame driver +
zoom-16 first pass terrain blit); chips 4–5 surveyed only.  Survey
landed 2026-05-25, chips 1–3 landed 2026-05-25.

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
   The zoom-32 sibling.  Larger because zoom-2 needs more
   per-cell setup (probably hex-row offset interleaving).

5. **`FUN_004bd355`** + **`FUN_004bd233`** + **`FUN_004bd07f`**
   + **`FUN_004bf1f9`** — chip 5 (or more).  The four passes
   that draw units/effects on top of terrain.  Each is its own
   discrete porting effort.

6. Helpers — interleaved as required:
   - `FUN_004bc222/2a5/2c5/2e5/346` (hit-test, screen↔hex) —
     ports cleanly as small pure functions; unblocks input.
   - `FUN_004bc1d1/51a` (scroll/pan) — possibly large state
     machine for keyboard/mouse panning.

Total cluster: ~12 KB across ~34 functions.  Comparable in
scope to strategic-mode rendering, which took 4 chips.

> 📍 Ghidra `FUN_004bc142` (entry), `FUN_004b49f7` (sprite
> primitive), `FUN_004b99c0` line 88581 (caller).
