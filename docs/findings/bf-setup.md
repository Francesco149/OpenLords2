# Battlefield port — partition `setup` (entering the battlefield)

Owner files: `src/engine_bf_setup.c` (+ `_globals.c`), `tools/rhost/rh_hooks_bf_setup.c`. Design: `battlefield-sim-port.md`.
All 50 functions ported (2026-11-04). Evidence: the commits' `rhost shadow:` lines; c0 = no corpus scenario reaches it.

## Call tree

```
FUN_004778a0 enter the battlefield (callers 0x43b593 0x43d5b7 0x43f5c5 0x444a2f 0x444a9b)
├─ terrain: siege s_slot_0053e8fc → FUN_0047c4ba(castle s_slot_0055401c)
│           custom battle, map set 3 → FUN_0047b8b2 (c0)      else → FUN_0047aaa3 (field)
├─ FUN_0047efee deploy both armies → FUN_0047fea7 (field) / FUN_004801f8 (siege defender) → FUN_00480662 per block
├─ FUN_00480f8b sprite handles, FUN_004822ed (unit), FUN_0047fc01 (group)
FUN_00477c89 re-enter a battle restored from a save (FUN_004976a1; c0)
FUN_0047ed9b per-frame cursor probe (engine_tick)
```

## What each does (short; the port's comments are the long form)

| fn | what |
|---|---|
| 4778a0 | music, battle clock, tick cadence; gate / side picks s_slot_0056d6b0 / 0053f654 (MP: cycled; else single-LFSR); per-battle state cleared; screen 0x29, sub-state 2, scroll (32, 33); terrain; deployment; one unit-driver pass; redraw; DAT_00544030 = 1 |
| 477c89 | castle s_slot_0055401c from the current army (+0x1c3/+0x1c1/+0x1f9/+0x1c0), stone flag s_slot_0057c910; sprites, tile scratch, handles, FUN_0047f42d |
| 47aaa3 | field map: s_slot_00553528 = next of 47 shuffled maps (s_slot_0057cae0, cursor s_slot_005653f8) or the custom map; code bytes → cell kinds; markers (20 attacker / 30 defender) → anchor tables; auto-tile (single-LFSR per cell); 4-cell features 7/8/9 |
| 47b8b2 | custom user map (FUN_0042d806): own code table; one marker per side → 12 anchors around it (offsets 0x4d9578, clamp 1..78); kinds 3 / 6 random tiles |
| 47c4ba | siege map: castle codes (0xee water, 0xed..0xef specials), heights / walk flags from 0x4d7b80 (stone) / 0x4d7d80, wall classes 5..12 → zones; FUN_0047cec1 markers; FUN_0047e230 zones; DAT_004eeb64 (gate anchor), DAT_004eeb6c (RNG roll vs the siege engines), FUN_004787a4 |
| 47cec1 | the castle marker layer (stnfield.pl8 / stnfiel2.pl8): field anchors + siege anchor rows s_slot_00554180 (attacker, 4-bit index FUN_0047d5ec) / s_slot_0055cd90 (defender, 2-bit FUN_0047d4b8; type 67 = 4 pairs at (col, row − 2)) |
| 47e230 | zones (+7) by six flood fills 47e263 (6) 47e387 (5) 47e52d (4) 47e668 (3) 47e7b2 (1, outside) 47e926 (leftovers) |
| 47efee | size tier s_slot_00522f6c (both armies' +0x168 and siege-engine counts through 0x4d9618 / 0x4d95d8), strengths s_slot_0054409c / 00553fc0 (class weights 0x4d4b98), "outnumbered by a third" s_slot_00543fc0 / 0056d624; tables cleared; attacker pos 4, defender pos 0 |
| 47fea7 / 4801f8 | troops per unit DAT_004eea9c (tier 0x4d9658, halved when the army is ≤ 8 units of it); classes in order 0x4d9870 (11) / 0x4d98a0 (8); blocks of ≤ DAT_004ee8f8 units; then the mercenary block (+0x195 class, +0x196 troops, special = 1) |
| 480662 | one block = a new group (FUN_0046e97a): formation +8 by class, units via FUN_0046e4c8 at FUN_00481878 / 481906 offsets from the anchor (FUN_0048169e field / FUN_004816f9 siege); per unit the class parameters (FUN_00480599) |
| 46e4c8 | first free unit slot DAT_0053f014, a free cell near the target (FUN_0046e70e → FUN_00496155 rings → the cell cursor), the record |
| 47ed9b | mouse → cell DAT_00553f88/94/68, kind DAT_00553eec; the unit under the cursor → DAT_0057cac8 / 00553f90 / 005533b8 |

## Quirks (retail ground truth; a naive port gets these wrong)

1. **FUN_0047ed9b counts with a stale index.** The own-unit count s_slot_00565404 tests `byte_554492[432 · DAT_0053e8f8] < 7`
   — the class of unit DAT_0053e8f8 (the last sweep's loop global, normally 81: past the 80-unit table, byte 0x55cd42), not
   of the unit being counted. The count is all own units or none, decided by byte 2 of the save dword s_slot_0055cd40 (< 7: count all).
2. **Overlapping auto-tile tables.** FUN_0047aaa3 / FUN_0047b8b2 reset and match the pattern table 0x4d7550 with n = 11, but
   it holds 10 entries: the 11th is 0x4d75c8's first entry (0x4d75c8 − 0x4d7550 = 120 = 10 × 12). So FUN_0046c40c(0x4d7550, 11)
   also resets 0x4d75c8[0]'s rotating counter and FUN_0046c2de can match that entry for a kind-4 cell.
3. **The cell cursor is engine_map_query's.** DAT_00569570 / DAT_00569578 are g_cell_cursor_col / _row (engine_map_query.h);
   every terrain pass uses them as loop globals and FUN_00496155 leaves its hit there. Under rhost, writing the g_bf_569570 alias
   was undone by rh_port_sync_out (fixed on master 5b4075a); the port uses the owning symbol. Same for DAT_0053e9ac
   (engine_territory_dirty_state()->below_status_redraw).
4. **The custom-map gate tables are swapped.** FUN_0047b8b2 writes the attacker marker's (kind 20) four gate pairs into
   s_slot_00544050 + 0x20 and the defender's into + 0 — the opposite of FUN_0047aaa3 (kind 20 → + 0).
5. **Siege-only formations.** FUN_00480662: when s_slot_0053e8fc (a siege) and pos 0 (the defender), the first formation-1
   block (classes 1 / 5) becomes formation 9 and the next two 10 (DAT_004eeafc / DAT_004ee8ec). FUN_004801f8 also forces
   3-unit blocks for classes 1 / 5 (after FUN_00480599 loaded the class row).
6. **FUN_004816f9's siege-pair cursor DAT_004d98c0 is .data and never reset** (it wraps after 8, or restarts at an empty pair):
   successive sieges continue the rotation. Its first argument is unused; so is FUN_00480662's third.
7. **Unit +0xc (animation phase) uses the block anchor x**, not the unit's: ((16 · x_anchor + 9 · slot) & 0x3f) + 0xb4.
8. **The marker slot lives two cells right.** FUN_0047aaa3's anchor slot is `byte_5440F0[off] − 64` = the kind byte of the
   cell two to the right (the map encodes slot n as code 64 + n there), clamped 0..11; FUN_0047cec1 reads it from the layer
   buffer (FUN_0047d6b0: byte o + 2 − 64, unclamped there, clamped by the caller).
9. **Two RNGs.** The terrain passes step the single LFSR FUN_00404b2c (DAT_00591564 → DAT_005c9a84) once per cell (field
   maps) or per special code (siege map, codes 0xed..0xef); FUN_0047c4ba's ladder roll DAT_004eeb6c uses the two-LFSR
   FUN_00404a46 (DAT_0058fd7c).
10. **FUN_0047e7b2 seeds zone 1** at cell 0 and at cell (col 0, row 79) (byte 0x550667) before its flood fill.
11. **FUN_004778a0, multiplayer:** FUN_0043fd2a is called with no argument (IDA's pseudocode shows FUN_0043f4b7's result
    passed; the asm pushes nothing).
12. **FUN_0047efee's strength** adds the mercenary block only when territory +0x197 is set, weighting +0x196 troops by
    0x4d4b98[+0x195]; the siege-engine classes 7..10 enter only the size tier.

## Seams (TODO in the code)

- `TODO(bf-link)` — a port exists but is not linked in rhost / not on retail's layout; retail runs via engine_retail_call:
  io_path_chdir_base 0x4af502, io_read_file_at 0x4af8d9 (io_path.c / io.c); engine_tile_refresh_path_scratch 0x471c1f /
  _stage_scratch 0x471d30 (engine_tile.c); engine_battlefield_render_screen_in_grid 0x4bc222 / _pixel_to_col 0x4bc2a5 /
  _pixel_to_row 0x4bc2c5; engine_tick_cadence_reset 0x40489e; castles_read_scenario 0x4aea70 (save.c; the port takes a
  path); engine_territory_pick_feature 0x46c2de (the port runs the strategic map's own table with a file-static counter —
  the battlefield needs the (table, n) form on retail's mask DAT_00543fe0..fc).
- `TODO(bf-extern)` — unported, non-battlefield: 0x481b9a (1399 B; the ledger calls it "ported" only because engine.c's
  comment names it), the debug log 0x4afab9, the multiplayer sync 0x43f4b7 / 0x43fd2a.

## Standalone notes (rhost exact; the standalone must reconcile)

- The pattern tables g_bf_4d7550 / 4d75c8 / 4d7610 / 4d7860 are 1-byte symbols in engine_bf_globals.h; they are 10 (+1) / 6 /
  49 / 17 × 12-byte tables (with retail's initializers).
- FUN_0042d806's file name buffer 0x53f5fc (= 0x53f1e0 + 0x41c) has no port symbol.
- The NEIGHBOUR flag on g_bf_543fe0..543ffc (the 8-neighbour mask) and g_bf_544030 is a false positive: they are fixed
  scalars, not grid-relative; the port uses the symbols.
- FUN_0047ed9b reads unit slot 81 (quirk 1): the standalone's g_per_battle_hex_432 has 81 records — the read is out of bounds.
- g_bf_591564 (the single LFSR) is new (engine_bf_setup_globals.c, seeded 0x34518632 as retail's InitInstance does);
  engine_territory.c threads its own copy for the strategic map.
