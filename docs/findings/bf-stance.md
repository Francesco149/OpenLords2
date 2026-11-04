# Battlefield port, partition 'stance' — the siege group AI (2026-11-04)

`src/engine_bf_stance.c` (28 functions, 12140 B), rows `tools/rhost/rh_hooks_bf_stance.c`. Design of the port:
`battlefield-sim-port.md`. Retail ground truth: IDA pseudocode + asm (`/mnt/ephemeral/openlords2/decompiled-ida/`).

## Dispatch

0x489401 (the group AI driver, partition 'group') walks the groups with its loop index **DAT_00568458 as a
global** — every handler here takes the current group from it. Non-siege: stance (+8) ≤ 4 through
DAT_004d91b8 (partition 'group'). Siege (DAT_0053e8fc): group +3 != 0 (the defending side) → DAT_004d91d0
(stance ≤ 8), else DAT_004d91f8 (stance ≤ 10).

| stance | defenders (DAT_004d91d0) | attackers (DAT_004d91f8) |
|---:|---|---|
| 0 | 0x48a9c7 (group) | 0x48a9c7 |
| 1 | FUN_0048d16e | FUN_0048e097 |
| 2 | FUN_0048d412 | FUN_0048e39c |
| 3 | FUN_0048d6fc | FUN_0048e4cc |
| 4 | FUN_0048d9ce (the lord: may surrender) | FUN_0048e774 |
| 5 | FUN_0048db84 | 0x48a9c7 |
| 6 | FUN_0048ddc7 | 0x48a9c7 |
| 7 | FUN_0048dfbb | 0x48a9c7 |
| 8 | 0x48a9c7 | FUN_0048e8b8 |
| 9 | — | FUN_0048e234 (clock only) |
| 10 | — | FUN_0048e2c8 |

## The handler skeleton

Every handler: group +0x1c (the clock) += 1; below 200 (100: FUN_0048e39c, FUN_0048e4cc) return; at the limit
most handlers wait while the group is busy (+0xd != 0) — the clock is NOT reset then, so it stays ≥ the limit and
the step runs on the first sub-step the group is free. FUN_0048e39c, FUN_0048db84, FUN_0048ddc7, FUN_0048dfbb
have no busy wait. Then the clock resets, one step runs, the step counter +0x1a += 1 (except where FUN_0048db84 /
FUN_0048ddc7 return early after ordering a member). A step's order = the anchor +0x22/+0x24 then
0x479e90(group, x, y, 0, 0, 0).

Siege state read (writers): DAT_00553fb0 / DAT_0053e990 breach counters (siege setup 0x47c4ba from the county
record; += 4 when the gate gives way: FUN_00496b9f, 0x483a88, 0x484a0c; DAT_00553fb0 = 500 outside a siege);
DAT_00542cd4 the attackers' kit flag (DAT_0055401c == 1; MP DAT_004d4ad8); DAT_00569588 the gate is down;
DAT_00553e64 the units on a kind-5 cell in formation slot 4 (FUN_004822ed); DAT_004eea98 the battle clock
(0x47fc01) vs its limit DAT_00552ff0 (0x4983b7).

## Helpers

- **FUN_00496b9f** — the gate gives way, once per battle (DAT_0052af9c): kit ≥ 3 (DAT_0055401c > 2; MP
  DAT_004d4b18[realm]); the first cell with +1 & 0x40 and the 4 × 7 block from it become kind 3; DAT_00569588 = 1;
  both breach counters += 4; 0x471c1f / 0x471d30 rebuild the path / stage scratch.
- **FUN_00495956 / FUN_00495c41** — nearest enemy unit to a unit (within `range` on both axes, |dx| / |dy| from the
  distance helper's side globals DAT_00591560 / DAT_0058fea0): Manhattan, 0 for the group's locked enemy (+0x2c),
  / 4 on order 9, ranged: / 2 (FUN_00495956) or × 2 (FUN_00495c41); class ≥ 7 targets only for a ranged hunter,
  +35. Pick → DAT_0052eff4. Loop index DAT_0056d5d0 (global).
- **FUN_00495f3d** — count enemies of `owner` in a clipped window on cells of kind ≤ maxkind; ≥ need → the first
  one's cell offset.
- **FUN_00496566** — nearest kind-4 cell of height ≤ maxh in a clipped window: Manhattan when maxh == 4, else
  0x405000 (= min(|dx|, |dy|)); ties → the LATER cell (`>=`); col / row → DAT_0057d328 / DAT_0057d324.
- **FUN_00496d76** — nearest kind-5 cell (see quirk 2). Result in DAT_00569570 / DAT_00569578.
- **FUN_0048ed95** — the footprint list DAT_00553c80 (save.h `g_footprint_list`, 20 × [cell offset, group]): the
  group's claimed cell, else claim the first unclaimed one.
- FUN_0048ea95 / FUN_0048ebf1 (enemies near / standing on the wall), FUN_0048cba9 / FUN_0048cce3 (a member's
  enemy → the nearest wall cell, kind 4), FUN_0048ce1d, FUN_0048ce81 (man the wall), FUN_0048d013 (fall back).

## Quirks (retail behaviour a naive port misses)

1. **Search loop indices are globals.** FUN_00495f3d, FUN_00496566, FUN_00496d76 and FUN_00496b9f run their row
   / column loops on DAT_00569578 / DAT_00569570 (also engine_map_query's cell cursor mirror), FUN_00495956 on
   DAT_0056d5d0: the final values are state, and results (FUN_00496d76, and 0x496768 whose DAT_00569570 /
   DAT_00569578 FUN_0048ce81 orders the group to). The standalone must keep g_bf_569570 / g_bf_569578 the one
   copy (engine_map_query's g_cell_cursor_col / _row mirror the same VAs: DUP).
2. **FUN_00496d76 measures from the window corner.** Retail clips the window by rewriting its own arguments
   (`sub [ebp+arg_0]`), then both "skip the centre" (`a1 != x0 || a2 != y0`) and the Manhattan distance use the
   CLIPPED top-left corner, not the centre: with r > 0 and the window inside the grid nothing is skipped and the
   pick is the kind-5 cell nearest the corner. (0x496768, partition 'action', has the same corner compare.)
3. **FUN_00496b9f's scan breaks only the inner loop** and its row stride local is still 0, so the scan runs
   linearly and every later row re-finds the same cell at once: the loop globals end at (80, 0), not at the
   found cell. The 4 × 7 block is written from the found cell with no bounds check.
4. **The busy wait keeps the clock** (above): a handler's step is late, not skipped.
5. **Defender cycle counters** DAT_004d9c74 / 9c80 / 9c84 / 9c88 are shared by ALL groups of that stance (one
   global per handler): with n groups the cycle advances n per round. The reset is `> 2 || < 0` (`> 5` for 9c84),
   signed.
6. **FUN_0048d412 / FUN_0048d6fc jump the step to 100** in one band when the kit flag is set — the band chain then
   continues from 101 (FUN_0048ce81 forever).
7. Comparisons are signed (`jg` / `jl` on the save-block dwords, `movsx` on the group / unit words); the cell
   bytes and the group owner byte are zero-extended.

## Evidence

`rhost.py verify --target wine --only bot3 --hooks '^bf_'` (the siege scenarios bot3-s103-t36, bot3s-s404-t36
reach the attacker table): see the commit messages. Never reached by the corpus (the AI never defends a castle
in it): the defender table (FUN_0048d16e … FUN_0048dfbb) and its helpers FUN_0048ce1d / FUN_0048ce81 /
FUN_0048d013, FUN_0048e774 (attacker stance 4), FUN_00496d76 (via 0x48c3fe). Those are ported from the asm and
checked by the probe below (poked state), not by the corpus.
