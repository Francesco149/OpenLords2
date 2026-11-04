# Battlefield port — partition `move` (2026-11-04)

The move order's step (FUN_0048f1dd) and everything below it: 26 functions, 19264 B, all ported
(`src/engine_bf_move.c`, rows `tools/rhost/rh_hooks_bf_move.c`). Design of the port:
[battlefield-sim-port.md](battlefield-sim-port.md). Retail's loop state is globals: the current unit
DAT_0053e8f8, the last probed occupant DAT_0052eff4, the step direction DAT_00554474.

## The step (FUN_0048f1dd)

Per call, for the current unit:

1. **Pacing** — `+0x34` bit 0 clear = a step under way: tick `+0x33` up to the pace `+0x189`, then the frame
   `+0x32` += 2 up to 16; past it the step completes (`+0x34 |= 1`). `wait_only == 1` stops there.
   (Retail has a multiplayer branch DAT_00553030 whose two arms are identical.)
2. **Heading** — `+0x169` = Chebyshev distance to the target `(+0x24, +0x26)`; DAT_00554474 = an adjacent
   enemy on the same elevation (FUN_004972f9), else the stored path's next step (FUN_00491a34, when the path
   flag `+0x164` is set) or the straight-line direction (FUN_0046f434). None → stop (path dropped, or
   `+0x34 |= 2`).
3. **Probe** the next cell: FUN_00490616 (1×1) / FUN_00490c59 (3×3 units, `+0x194`) → FUN_00490a44 /
   FUN_00490fa2 / FUN_00491147. Results: 1 free, 0 friend / soft-blocked, 2 blocked, 5 (flag 0x20 for a unit
   in a formation slot → order 6), 6 (a 3×3 unit in state 9 meets 0x20 / 0x40 → order 14), 999 enemy.
4. **Enemy** → both units engage (order 4, `+0x17e` the foe, `+0x185` = 1 / 2, FUN_0048f079 releases each
   group's hold, FUN_00494d08 / FUN_00494df9). **Friend of the same group** → FUN_0049005f: swap places (2),
   wait (0) or blocked.
5. **Blocked** → a side step (FUN_004904ec, only next to the target), else re-plan after a 64-frame cooldown
   `+0x165`: the path field (FUN_00471718 for order 9, else FUN_0047095e), the detour test FUN_00472227, the
   greedy walk FUN_00472392 (fallback: the nearest reachable cell FUN_0047265d), stored as the unit's path
   (FUN_00472875). Re-plans counted in `+0x166`, failures in `+0x176` (gives up at 4).
6. **Step** (free) → FUN_00491b1f moves x / y / cell offset and the occupant byte.

## The path field (FUN_0047095e and below)

DAT_00504030 (80×80 i16) is rebuilt per re-plan from the base costs DAT_00507250 (`g_path_scratch`, the
outer ring forced to 999 by FUN_00471c1f). FUN_00471f45 marks friendly units (998) — other groups', and own
group units that are not moving — plus each 3×3 unit's ring. FUN_004710f2 first tries two cheap
wall-following probes (clockwise / anticlockwise from the direction to the target, up to 80 rounds); only
if both fail does FUN_0047095e run the BFS (ring queue DAT_004fa820, 6400 entries) with per-cell delays
DAT_004f2770 (a cell is re-queued until its wait counter DAT_004f6470 passes its delay; the delay is also
added to the neighbour's cost). Expansion order N, NE, E, SE, S, SW, W, NW; one elevation band (DAT_004f7d80).

## Quirks (retail behaviour a naive port would miss)

1. **FUN_004928a3 is a no-op**: its first instruction jumps to the epilogue. The dead body would re-target a
   unit stuck on a shared target at a free cell (FUN_00496155) or remove it. FUN_0049005f calls it when two
   units of a group head for the same cell — so nothing happens and the mover is just "blocked" (0).
2. **The BFS clears only 4096 of 6400 wait counters** (FUN_004b3e51(DAT_004f6470, 0x1000)): cells with
   index >= 4096 (row 51 col 16 onward) keep the (u8) counts of earlier searches: once a cell's count has
   passed its delay, later searches no longer delay it — until the counter wraps at 256.
3. **FUN_0047095e tests the PROBES' target** — DAT_004f0378 is reassigned only after the 998 / >= 999 check,
   which still reads the value FUN_004710f2 left (the same cell, as both get the same target).
4. **FUN_00491b1f takes an occupied cell** when the LAST PROBED unit DAT_0052eff4 (not the occupant) and the
   occupant are both in movement state 8.
5. **FUN_00490a44 vs FUN_00490fa2**: the 1×1 probe lets elevation 5 through any step and fires FUN_0048551d on
   a kind-7 cell for a slot-4 unit; the 3×3 edge probe has neither, and counts any occupant as blocking.
6. **FUN_00496155's cursor**: the free-cell box scan's loop variables ARE the shared cell cursor
   DAT_00569570 / DAT_00569578 — the hit's cell on success, the box's end on failure. Its callers
   (FUN_0046e70e, FUN_004923a8) read them.
7. **FUN_0048f1dd on a dead unit returns 432 × the unit** (EAX left by the address computation): non-zero,
   so its callers take it as "still moving".
8. **Signed bytes**: the step frame `+0x32`, tick `+0x33` and flags `+0x34` are compared signed (`movsx`);
   the direction `+0x18` is a signed switch (a negative value: no step).
9. **Field border**: none of the field functions bound their neighbour accesses; the 999 outer ring keeps
   the BFS and the greedy walk inside, except from a unit standing ON the border (its own cell is set to 1).

## Seams

- `engine_path.c` (linked in rhost) runs on a state struct: FUN_00472392 / FUN_0047265d / FUN_00472875 are
  called through wrappers that load the struct from retail's globals and store back what changes
  (DAT_004f9680 — `g_bf_4f9680`, defined in `src/engine_bf_move_globals.c`, DAT_004f7d70, the distance deltas,
  the cell cursor). **engine_path_find_nearest_matching leaves a different cursor than retail after a miss
  over all 30 radii** (retail: the end of the radius-29 box; the port: the last in-band cell) — the wrapper
  puts retail's back; the port should be fixed there.
- `engine_tile.c` is not linked in rhost: FUN_00491492 calls FUN_004921e5 / FUN_00471c1f / FUN_00471d30 /
  FUN_0048ee46 through `engine_retail_call` (TODO(bf-link)).
- Mirrored globals: DAT_00569570 / 78 are engine_map_query's `g_cell_cursor_col / row`, DAT_00591560 / 0058fea0
  engine_path's `g_path_dist_dx / dy` — the move port goes through those names.

## Evidence

`RHOST_JOBS=3 rhost.py verify --target wine -j 3 --only bot3 --hooks '^bf_'` (2026-11-04, 14 bot3
scenarios, 13 on the field): the 26 move rows, 4295640 calls, 0 divergences; hybrid PASS. Reached 19 / 26
(bf_48f1dd 2597594 calls, bf_490a44 369847, bf_4972f9 361660, bf_490616 352745, bf_4904ec 228053, bf_49005f
105118, bf_491b1f 96106, bf_494df9 74534, bf_491a34 48860, bf_47095e 13143, bf_472227 13143, bf_4710f2 13123,
bf_494d08 10853, bf_4928a3 3613, bf_48f079 3599, bf_496155 1903, bf_4b3dcd 872, bf_471f45 872, bf_49207e 2).

**Unverified (no corpus evidence: never called)** — the 3×3 units (`+0x194`, siege engines) and order 9:
FUN_00491f17, FUN_00490c59, FUN_00490fa2, FUN_00491147, FUN_004912ec, FUN_00491492, FUN_00471718. Ported from
the disasm; a scenario with a siege engine on the field (and a unit in order 9) would cover them.

First divergence met (fixed): bf_496155 1344 / 1344 — it wrote DAT_00569570 / 78 through `g_bf_569570`, and
rh_port_sync_out stored engine_map_query's stale mirror over it (rhost trap, fixed on master 5b4075a; the port
goes through the mirror anyway).
