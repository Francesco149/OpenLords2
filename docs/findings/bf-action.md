# Battlefield port — partition `action` (2026-11-04)

The unit order table DAT_004d9170 and its private helpers: `src/engine_bf_action.c`, rows
`tools/rhost/rh_hooks_bf_action.c`. Design of the port: [battlefield-sim-port.md](battlefield-sim-port.md).
All 36 functions ported; evidence in §Verification.

## The order table

A unit's order is byte +0x31 (`byte_5544B1`, 0..0x11). The movement-state handlers (DAT_004d9140, partition
`unit`) set the step parameters, then call `DAT_004d9170[order]` (an order outside 0..0x11 removes the unit).
The current unit is the per-unit driver's loop global DAT_0053e8f8; the current target DAT_0052eff4 — retail
re-reads both at every access and callees change them, so the ports never cache them across a call.

| # | VA | order | what |
|--:|---|---|---|
| 0 | 482f86 | — | empty |
| 1 | 482f91 | hold | missile units (+0x16a) with countdown +0x30 > 5 look for a shot (FUN_00483337); the countdown (frozen by DAT_005c9268) restarts at 5 and falls back to the default order +0x2f |
| 2 | 4830e9 | dying | +0x173 counts to 80, then the unit is removed (FUN_0046ebe4) |
| 3 | 48314e | move | a path step (FUN_0048f1dd(0)); arrived (+0x34 bit 1) → 5 |
| 4 | 4831d8 | melee | opponent +0x17e; not itself in melee (dead / order != 4) → 8; else the exchange FUN_00494908 and, engaged (+0x34 bit 0 clear), a closing step |
| 5 | 4832ea | idle | missile units look for a shot |
| 6 | 483a88 | mine | DAT_00554034 (+1 on a kind-5 cell) / DAT_00568da4 (+1 elsewhere) → breaches (FUN_0049694f); three dig targets above (FUN_00496f72) |
| 7 | 483ce1 | engage | an adjacent enemy (FUN_00494e3f) → both into melee (4); none → 8 (FUN_004956cc finds one) or 5 |
| 8 | 483e55 | pursue | target +0x174 or the nearest enemy (FUN_004954dd); its position is the destination; siege units → 11 |
| 9 | 483fe1 | fill moat | work counter +0x18e on cell +0x190 raises the fill level; full (DAT_00554090) → land (FUN_0047dd86); moat cells searched in rings (FUN_004926fb / FUN_004927fa) |
| 10 | 4842c6 | siege move | siege thunks FUN_00488436 / FUN_00488793; arrived → 11 |
| 11 | 48437e | siege idle | |
| 12 | 4843bc | siege fire | aim point +0x28/+0x2a (group +0x16/+0x18); out of range → walk; pending regroup (+0x183) → the group to the side's rally point (DAT_0055ce30[DAT_0056d6b0], FUN_00479e90); reload +0x180: at 100 a projectile, at 180 restart |
| 13 | 4849fc | — | siege thunk FUN_0048895e only |
| 14 | 484a0c | siege ram | as order 6 at 20 a tick; ends (11) after two wall breaches or the gate breach |
| 15 | 484b89 | siege wreck | +0x173 to 120, then removed |
| 16 | 484bee | — | empty |
| 17 | 484bf9 | shoot | target +0x174 or the group's area +0x30; range +0x170 / 8 vs Chebyshev + 2; volley when reload +0x180 passes +0x16b |

Helpers: FUN_00494908 (one melee exchange: damage accumulator +0x19a in 1/100 men, the exchange phases
+0x184 / +0x185, FUN_00494d08 / FUN_00494df9), FUN_0047a814 (a throw: projectile + 4 flight steps + impact),
FUN_004954dd (nearest-enemy pick), FUN_0049694f (wall breach), FUN_00471da6 (the 80×80 blocker grid
0x4f2770), FUN_00494e3f (adjacent-enemy scan), FUN_0047dd86 (a moat cell filled: re-tiles every moat cell),
FUN_004926fb / FUN_004927fa / FUN_00496768 / FUN_00405000 (moat-cell search), FUN_00479e90 (the group order:
called by the stance tables, the siege fire, the interactive orders), FUN_0046e97a / FUN_0046ebae (group
allocation), FUN_0048143b (group stance from its units), FUN_00497437 / FUN_0048a7d9 / FUN_0048a672 (pull the
target into range; room for the group).

## Quirks (retail behaviour a naive port misses)

1. **FUN_00479e90 clobbers the per-unit driver's loop index.** Its split-off path (a4 with mixed missile + foot
   units) loops `for (DAT_0053e8f8 = 1; < 81; …)` twice — the global FUN_004822ed iterates. Called from order
   12 (FUN_004843bc, inside the per-unit loop) it would end that sub-step's unit loop at 81. Kept.
2. **FUN_00496768's "skip the centre" compares the box's clipped origin with (x, y), not the cursor**: it skips
   the whole box when the origin equals (x, y) (radius 0, or a unit at (0, 0)) and never skips the centre cell.
3. **FUN_0048a7d9 returns the TARGET, not the cell it found**: on success the cursor globals are set to (tx, ty),
   so "room near the target" only gates the move; the found cell is discarded.
4. **FUN_00471da6 writes past its grid**: the wedge below a flagged cell (+0x50 … +0x141) runs past 0x4f4070 for
   the bottom four rows (into DAT_004f4070, the strategic path field's source cell, and beyond). The generated
   block `g_bf_4f276f` ends at 0x4f3ffe (IDA's name `word_4F3FFE` there is the strategic grid's row -1), so
   even the 6400-byte clear runs 0x72 bytes past it: the port launders the base pointer (GCC's
   `-Warray-bounds` / UB reasoning); the standalone must widen the block (0x4f276f..0x4f4070 + 0x142).
5. **FUN_0049694f tests DAT_0057c910 and adds 16 either way** (both arms identical).
6. **The cursor globals DAT_00569570 / DAT_00569578 are loop indices** in FUN_0049694f, FUN_00496768,
   FUN_0047dd86 and outputs of FUN_00497437 / FUN_0048a7d9 / FUN_00496768: they are engine_map_query's
   file-static mirrors `g_cell_cursor_col` / `_row` under rhost (rh_port_sync_in/out), so a port must use
   those names — a direct store to `g_bf_569570` is overwritten by the stale mirror at sync_out.
7. **FUN_00405000 = min(|dx|, |dy|)** and leaves the deltas in DAT_00591560 / DAT_0058fea0 (engine_path's
   `g_path_dist_dx` / `_dy` mirrors) like the distance helpers; FUN_00497437 reads the Manhattan helper's
   deltas, FUN_0047a814 picks the facing from them (|dy| > |dx|: up / down).
8. **FUN_00494e3f leaves DAT_0052eff4 at the last occupant looked at** (0 for a siege unit) even when it finds
   nothing; FUN_00483ce1 then stores it as the opponent only on a hit.
9. **FUN_00483e55 reads unit 0's order** when the pursuit pick returns 0 (slot 0 is the unused record).
10. **FUN_00479e90's area order writes unit 0's position** (`word_5544A0` / `word_5544A2` with no index).
11. **FUN_0048143b scans the group's index range without checking the records are alive** (stale slots count).
12. **FUN_004832ea loads +0x178 into a dead local** (stack residue only).
13. **FUN_00479e90 is `void` to IDA but its EAX escapes**: partition `input` returns it. The port models it: g
    for g = 0 (all 29 call sites do `mov eax, g; push eax`), 13 × g for a group without +2 or a moat target (the
    group-record index left in EAX), else FUN_00488dfe's value (retail 0x410 = 13 × 80, its last loop index; the
    group port returns 0, so the row is HOOK_V — no call site reads it). Likewise FUN_0048143b returns 13 × g,
    FUN_00497437 the last cursor axis written (or range − 3), FUN_00471da6 its last loaded value.

14. **The siege orders feed stack garbage to the sequencers.** Orders 10..15 (FUN_004842c6, FUN_0048437e,
    FUN_004843bc, FUN_004849fc, FUN_00484a0c, FUN_00484b89) call the class 7..10 sequencers 0x488793 / 0x488436 /
    0x48895e for any unit; for another class those store their unset `[ebp-4]` into the sprite frame
    (bf-unit.md quirk 1, seams `bf_unit_res_*`). That slot is a fixed spot below the handler's entry esp e0:
    e0 − 0x1c − 4 × the handler's locals (0x1c; FUN_00484a0c 0x20, FUN_004843bc 0x28), `[ebp-8]` 4 lower. Its
    value is what the handler's previous call left there — a sequencer: its own final local; a 1-argument call
    (0x48f1dd, 0x46ebe4, 0x46efbe, 0x426120): its saved ebp = the handler's ebp (e0 − 4); a k-argument call
    (k = 3..6): argument k − 3 AS THE CALLEE LEFT IT (FUN_0049694f clips its x argument in place: max(x − 4, 0);
    the Chebyshev helper / 0x46f525 (x, y, …) → y; 0x46e767 → y; FUN_00479e90 → 0), slot 2 the next lower push
    (or the return address for k = 3). The port carries both
    slots in `bf_action_res[2]` through each handler; the six rows' adapters load them and e0 (a custom hook
    body records e0) and put slot 1's final value back (compared in shadow).

## Seams

- FUN_00427990 (the WAV cue, `bathit2.wav` at 0x4d9f38): `engine_retail_call` — TODO(bf-extern).
- FUN_00471c1f (`engine_tile_refresh_path_scratch`, engine_tile.c is not linked in rhost): `engine_retail_call`
  — TODO(bf-link).
- FUN_0046c2de (`engine_territory_pick_feature`): the port is not retail-layout (its own pattern table copy and
  variant counters, a mask argument instead of DAT_00543fe0) — retail is called on DAT_004d7610 — TODO(bf-link).
- `g_bf_4d7610` is declared as one byte; it is the 49 × 12-byte tile pattern table (standalone).

## Verification

`rhost.py verify --target wine --only bot3 --hooks '^bf_'` (11 scenarios, 2026-11-04, before the bot3s siege
games): 22/40 reached, 0 divergences, hybrid PASS. Per hook (calls / scenarios): bf_4832ea 2560887/10,
bf_483e55 973562/10, bf_48314e 820188/10, bf_4831d8 622315/10, bf_494908 620373/10, bf_482f91 101594/10,
bf_48a672 41379/10, bf_4830e9 38217/10, bf_48437e 7352/1, bf_46ebae 2240/10, bf_4954dd 2068/10,
bf_484bf9 1031/1, bf_4842c6 487/1, bf_479e90 464/10, bf_483a88 223/1, bf_46e97a 179/10, bf_471da6 28/10,
bf_47a814 1/1.

With the 3 bot3s siege games (14 scenarios, only the action rows, so the order handlers are the outermost
hooks): 18/36 reached, 7913207 calls, 0 divergences, hybrid PASS — bf_4832ea 3371102/13, bf_483e55 1361128/13,
bf_48314e 1231612/13, bf_4831d8 786904/13, bf_494908 784460/13, bf_482f91 230440/13, bf_48a672 61878/13,
bf_4830e9 52369/13, bf_483a88 12217/2, bf_48437e 8266/2, bf_484bf9 5058/3, bf_46ebae 3040/13, bf_4954dd 2635/13,
bf_4842c6 974/2, bf_479e90 819/13 (its modelled EAX compared), bf_46e97a 265/13, bf_471da6 38/13, bf_47a814 2/2.
Later runs: the commit messages (`rhost shadow: …`).

**Poke exploration** (not corpus evidence: rhbot sessions replaying `bot3-s103-t36` with rows in shadow mode and
the battle's foot units' orders / cells poked so the branches run — port vs retail on identical, poked state;
scripts were scratch): all 0 divergences after one fix. Orders 7 / 9 / 12..15 on foot units (the siege orders'
residue path, slot 1 compared): FUN_00483ce1 12, FUN_00483fe1 8916 (kind-2 cells planted around the units, one
filling at a full cell), FUN_004843bc 2673, FUN_004849fc 3472, FUN_00484a0c 1006, FUN_00484b89 243,
FUN_0048437e 2889; orders 0 / 16 / 10 / 11: FUN_00482f86 9475, FUN_00484bee 9060, FUN_004842c6 5081,
FUN_0048437e 8004; the helpers alone: FUN_00494e3f 12, FUN_004926fb 7506, FUN_004927fa 5117, FUN_00496768
5248, FUN_00405000 5626, FUN_0049694f 1 (DAT_00554034 poked to 4990 under order 14), FUN_0047dd86 4. The fix:
FUN_00484a0c's slot 1 after FUN_0049694f is the x argument as the callee clipped it in place (x − 4), not x
(`DIVERGE bf_484a0c … res e0-20 r00000036/p0000003a`). Not reached even poked: FUN_00479e90's attack / area
branches (FUN_0048143b, FUN_00497437, FUN_0048a7d9) — the AI stances pass a4 = a5 = 0, and the bot's order
clicks never reached the interactive attack path with planted archers, hover targets or a kind-15 cell.

Coverage c0 functions (no corpus evidence): FUN_00482f86,
FUN_00483ce1, FUN_00483fe1, FUN_004843bc, FUN_004849fc, FUN_00484a0c, FUN_00484b89, FUN_00484bee, FUN_0049694f,
FUN_00494e3f, FUN_0047dd86, FUN_004926fb, FUN_004927fa, FUN_00496768, FUN_00405000, FUN_0048143b,
FUN_0048a7d9, FUN_00497437 — ported from the asm, unverified.
