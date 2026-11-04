# Battlefield port — partition `strike`: missiles, impacts, damage (2026-11-04)

`src/engine_bf_strike.c` (22 functions, 12993 B; rows `tools/rhost/rh_hooks_bf_strike.c`). Design / porting loop:
`battlefield-sim-port.md`. Evidence: `rhost.py verify --target wine --only bot3 --hooks '^bf_'` — 17 of 22 reached,
0 divergences, hybrid PASS; 5 never reached by the corpus (c0, ported from the disasm, unverified): FUN_00485b47,
FUN_00485861, FUN_0048551d, FUN_0047da54, FUN_0047dfe0.

## The records

The save block's "combat unit" table (`g_per_combat_unit_76`, retail 0x57a100, 101 × 0x4c; accessors `BC*`) is the
battlefield's **projectile / effect** table. Fields (offset from the record, 0x57a100 + 0x4c·p):

| off | type | meaning |
|---|---|---|
| +0x00 | i32 | sprite blob (DAT_00566518 at spawn) |
| +0x04 | i16 | next record in the cell's chain (FUN_0046efbe) |
| +0x06 | i16 | shooter (unit index) |
| +0x08 | u8 | side (0 = free slot) |
| +0x09 | u8 | missile kind: 1 arrow, 2 bolt, 3 stone, 4 spent stone, 5 effect (fire / flash) |
| +0x0a / +0x0c | i16 | position x / y in 1/32 cells |
| +0x0e / +0x10 | i16 | target x / y in 1/32 cells |
| +0x12 | i16 | sprite frame (speed + heading) |
| +0x14 / +0x16 | i16 | cell x / y ((pos + 8) / 32) |
| +0x1c | i32 | cell byte offset 8·(x + 80·y) into the cell grid |
| +0x20 / +0x24 / +0x28 | i32 | Bresenham \|dx\|, \|dy\|, error |
| +0x2c | u8 | major axis (1 x, 2 y) |
| +0x2e | u8 | 8-way heading (0 N, clockwise) |
| +0x2f | u8 | start height (the shooter's cell +4) |
| +0x30 | u8 | fall age (dies at > 32) |
| +0x31 | **i8** | sub-steps per tick (movsx: signed) |
| +0x32 | u8 | copy of +0x31 |
| +0x34 | i16 | speed |
| +0x36 / +0x38 | i16 | age / life (life = the shooter's range +0x170) |
| +0x3a | u8 | falling |
| +0x3c | i16 | cooldown (no hit while != 0; effect timer) |
| +0x3e | u8 | the cell kind under an effect |
| +0x40 | i16 | damage |
| +0x44 | i32 | gate target: a cell offset (the group's +0x30, siege fire 0x484bf9) or 1 (archers, FUN_00483337) |

Cursors are retail globals: DAT_00568990 (the slot FUN_0046e767 found), DAT_0056d5b4 (the record in flight),
DAT_0053e8f8 (the shooter), DAT_0052eff4 (target / unit hit), DAT_0056d5d0 (FUN_004956cc's loop).

## The functions

- **FUN_00483337** — the unit's fire order (via the order handlers 0x482f91 / 0x4832ea): reload counter +0x180 vs
  the draw point +0x16b; at +0x16b − 10 the target pick (FUN_004956cc, range +0x170 / 8) and the facing
  (FUN_0046f525); past +0x16b the shot: FUN_0046e767, fields from the unit (kind +0x16a, speed +0x16e, sub-steps
  +0x16c, damage +0x19e cut by the fatigue +0x197: 4/5, 3/4, 1/2), then **8 flight ticks at once** (FUN_00492c8b),
  the cell chain (FUN_0046efbe), the frame, the cue (bolt 9, arrow 7). Then the unit faces its target (0x486239).
- **FUN_00492c8b** — one flight tick (fields above): gate hit (kind-15 cell with +0x44: flash, DAT_0053e9d0 = 1);
  fall / life expiry; +0x31 Bresenham sub-steps (FUN_00493b61, FUN_00494125 / FUN_004941c5, past the end
  FUN_00494265; off the field FUN_00494075); then, cooldown 0: wood (kind 7) catches fire (FUN_0048551d); a stone
  on a wall (kind 4, overlay +3 > 2, height 1..3: the wall's hit counter +0, at 16 FUN_0047dfe0; taller: bounce) is
  spent; an arrow / bolt / stone on a live enemy (cell +5) hits: see Damage.
- **FUN_0046e767** spawn, **FUN_00493cb9** line set-up, **FUN_0046efbe** cell chain, **FUN_0046eb78** record zero.
- **FUN_004956cc** — the nearest live enemy within `range` on BOTH axes (\|dx\|, \|dy\| from the Manhattan helper's
  side effect DAT_00591560 / DAT_0058fea0), Manhattan distance < 160; the group's remembered target counts as 0;
  siege-class targets (+0x12 >= 7) cost +35 and are skipped by units without a missile kind (+0x16a == 0).
- **FUN_0046f525** — 8-way facing, snapped to an axis when one delta >= 3× the other (FUN_0046f434 is the plain one).
- Impact effects: **FUN_00485675** (cell burns: effect record, cell kind → 10; wood also loses height / overlay),
  **FUN_00485861** (impact flash: cell kind → 16), **FUN_0048551d** (fire spread: the "dest_ind.wav" cue, five
  growing squares around the cell; wood next to fire catches (7 ticks sooner each, up to 120), kind 5 is scorched,
  **FUN_00485b47**), **FUN_00496f72** (4-neighbour kind test), **FUN_0047dfe0** (wall → rubble kind 9; the whole
  field's rubble re-tiled from the 8-neighbour mask **FUN_0047da54** through retail's pattern picker 0x46c2de on
  the battlefield table DAT_004d7930).
- **FUN_00426120 / FUN_004262cf** — play the preloaded cue buffer DAT_00522afc[id] (GetStatus, Play through the
  IDirectSoundBuffer vtable; HRESULTs in DAT_00522b58). Also the strategic map's movement cue (0x466d84).

## Damage (FUN_00492c8b)

`dmg = +0x40`; the target cell's height (+4) vs the shot's start height (+0x2f): targets with +0x13 set (older RE: a condition
flag) /4 when more than 3 above, /2 when more than 1; others /5, /3. Minus the target's armour +0x172. Siege-class targets: bolts /2
(+0x13) or /3; arrows doing <= 0 do 6 / 4; capped at 2·(5·DAT_00522f6c + 10) (+0x13) or 2·(5·DAT_00522f6c + 5).
At least 2. The wound accumulator +0x19a += dmg; at >= 100 (siege class 160) the unit loses 1..4 troops (+0x1a0;
the multiple of the threshold, capped at 4) and the accumulator resets to 0. At 0 troops: +0x31 = 2 (dead), cue 13.
Cues: bolt 10, arrow 8 (played again when troops are lost).

## Quirks (retail behaviour the port keeps)

1. **A full projectile table writes record 101 = player 0's strategic record.** FUN_0046e767 leaves DAT_00568990 = 101
   when no slot is free; FUN_00483337 and FUN_00485675 do not check and fill record 101 at 0x57befc..0x57bf48 —
   which overlaps `g_player_strategic_352[0]` (0x57bf00). The standalone must keep that adjacency (or reproduce the
   write) to stay faithful; under rhost it is retail memory.
2. FUN_00483337 picks the shot's cue from DAT_00568990 AFTER the 8 flight ticks: an impact flash / fire spawned in
   those ticks moves DAT_00568990 to the effect record (kind 5: no cue).
3. A shot that dies within its first 8 ticks is still linked into a cell chain by FUN_0046efbe — its cell offset is 0
   after FUN_0046eb78, so cell (0, 0)'s chain head (+6) gets the dead record.
4. FUN_00492c8b's gate branch clears unit **slot 0**'s side byte (`byte_5544AC[0] = 0`, the unused slot).
5. +0x31 (sub-steps a tick) is read signed (movsx); a negative count flies no steps.
6. FUN_0046f525 returns its facing in AL only; EAX's upper bytes are y0's (every path loads arg 2 first). The rhost row
   models it; port callers store a byte.
7. FUN_00426120 skips the Play only when the status is exactly 1 (DSBSTATUS_PLAYING) — a looping playing buffer
   (status 5) is restarted.
8. FUN_0048551d scans the whole square at each radius (inner cells re-scanned) and cells off the field: offsets wrap
   into neighbouring rows or fall outside the grid (retail reads there; so does FUN_00496f72 / FUN_0047dfe0's
   neighbour tests at the field's edge rows: `byte_543E67[off]` at row 0 is 0x280 below the grid).
9. The wound accumulator drops the remainder on a troop loss (a 399 hit at threshold 100 = 3 troops, 0 carried).
10. FUN_004956cc's remembered target still has to pass the range test (distance 0 only wins the comparison).
11. The gate branch of FUN_00492c8b fires for an attacker's shot on any kind-15 cell, a +0x13 shooter's only AT the
    target cell +0x44 — an archer's +0x44 is the flag 1, never a cell offset (multiples of 8): such an archer's arrow never
    takes it.

## Seams and shared-file notes

- Retail through `engine_retail_call`: 0x427990 (WAV cue loader, ported elsewhere, unlinked — TODO(bf-link)),
  0x48ee46 / 0x471c1f / 0x471d30 (engine_tile.c, unlinked — TODO(bf-link)), 0x46c2de (engine_territory_pick_feature
  is not retail-layout: it hardcodes the strategic table 0x4d7310, 48 × 11 B, counters file-static; retail's takes
  the table pointer — the battlefield's is DAT_004d7930, 49 × 12 B, counter at +11 — TODO(bf-link)), 0x4afab9
  (status-line logger, unported — TODO(bf-extern)).
- Mirrored globals go through their owners: DAT_00591560 / DAT_0058fea0 = `g_path_dist_dx / dy` (engine_path),
  DAT_00569570 / DAT_00569578 = `g_cell_cursor_col / row` (engine_map_query) — never `g_bf_591560` & co.
- `engine_bf_globals.h`: `g_bf_522afc` is declared a scalar, but retail indexes DAT_00522afc[id] (the cue buffers,
  ids 1..20, = audio.c's DAT_00522b00[20] − 1): it should be an array (0x522afc..0x522b58). The port reads it
  through `AT(…, &g_bf_522afc, …)` — exact under rhost, out of bounds in the standalone until fixed.
- `g_bf_543fe0` is flagged NEIGHBOUR of the cell grid, but DAT_00543fe0[8] is the 8-neighbour mask (one array with
  `g_bf_543fe4 … g_bf_543ffc`; 0x46c2de reads it as an array). The port writes the eight scalars.
