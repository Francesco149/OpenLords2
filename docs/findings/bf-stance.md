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

**Corpus** — `rhost.py verify --target wine --only bot3 --hooks '^bf_'` (2026-11-04): the siege scenarios
bot3-s103-t36 and bot3s-s404-t36 reach the attacker table — 16 of the 28 hooks, 151.0k calls, 0 divergences;
hybrid PASS over the 14 bot3* scenarios. In both sieges the AI is the ATTACKER (its groups +3 = 0); the player's
groups (+1 = 1, side +3 = 4) are skipped by 0x489401, so the defender table never runs in the corpus.

**Probe (poked state)** — not in the corpus: the defender table, FUN_0048ce1d / 0048ce81 / 0048d013, FUN_0048e774,
FUN_00496d76. A `rhbot.py` Session replays bot3-s103-t36 to frame 41400 (the siege battle runs 41200..44800),
pokes the AI groups g5..g12 and shadows the hooks to the end of the trace:
- `def`: +3 = 1 (defender table), stances 1..7 round robin; `defhi`: the steps +0x1a started at 12..98 (the
  later bands); `b`: both breach counters poked (DAT_00553fb0 = 6, DAT_0053e990 = 4) for the breach branches;
- `att4`: attacker stance 4 on half the groups, stance 3 aimed at a player group (+0xa / +0xc) on the others, and
  4 footprint cells (DAT_00553c80) planted near that group (the list is empty in this siege).

| run | shadowed | calls | diverge |
|---|---|---|---|
| def | 0x48d16e / 48d412 / 48d6fc / 48d9ce / 48db84 / 48ddc7 / 48dfbb | 5575 / 9793 / 9793 / 2397 / 2500 / 4757 / 9793 | 0 |
| defhi + b | the same 7 | 12088 / 2603 / 9793 / 9793 / 4086 / 9793 / 9793 | 0 |
| def, defhi, defhi + b (helpers) | 0x48ce81 / 48d013 / 496566 | 37 / 67 / 180 | 0 |
| att4 | 0x48e774 / 48e4cc | 39172 / 9082 | 0 |
| att4 (helpers) | 0x48ed95 / 496d76 | 76 / 15 | 0 |

Hybrid on the poked runs: all 28 stance hooks in replace mode end at the same state hash as the pure run
(state_sem def 426011a4526ca055, defhi + b 6f584ae8352e4aed, defhi 588ab64efb191c45, att4 0414d9d2af29719c).
bot3s-s404-t36 (siege battle 45800..60800, AI attacking again) poked at 46000 with `defhi + b`: 0x48d013 /
0x496566 12 / 331 calls, 0 divergences.

**Unverified**: FUN_0048ce1d (100 B: 0x496566(centroid, 40, 4) → 0x48c363) — only FUN_0048d16e's breach branch
calls it, with DAT_00553f74 > DAT_0056d6ac (0x481b9a recomputes both); no probe reached it (also tried with the
reserve DAT_0055307c poked to 0). Ported from the asm.

Pitfall met: `rhbot.Session` runs `C:\ol2-rhost\bin\ol2-rhost.exe` in the shared wine root, which `wine_stage`
overwrites only when older than the caller's build — with agents in parallel worktrees a Session can run another
agent's build ("FATAL unknown hook"). The probe set `rhost.WEXE` to its own name.
