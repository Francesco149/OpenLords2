# Battlefield port, partition `unit` (2026-11-04)

`src/engine_bf_unit.c`, rows `tools/rhost/rh_hooks_bf_unit.c`; design `battlefield-sim-port.md`. The per-unit
driver, the movement-state table DAT_004d9140, the troop band, the animation sequencers, the terrain attrition.
All 31 functions ported.

## Unit fields (record 0x554480 + 0x1b0·u; u = the current unit DAT_0053e8f8)

| off | VA (slot 0) | what (as these functions use it) |
|---|---|---|
| +0x0b / +0x0c | 0x55448b / c | frame tick / period (FUN_004872ae) |
| +0x0d | 0x55448d | drawn direction (every sequencer stores facing or move direction here) |
| +0x0e | 0x55448e | animation phase (each sequencer has its own wrap: 0x28, 0x18, 0x60, 0x30, 0xa0, 0x90) |
| +0x10 / +0x11 | 0x554490 / 1 | sprite frame / overlay frame |
| +0x12 | 0x554492 | class 0..10: the state-table index; written only at spawn (FUN_0046e4c8) |
| +0x13 | 0x554493 | condition flag |
| +0x18 / +0x19 | 0x554498 / 9 | facing / move direction (signed bytes) |
| +0x20 | 0x5544a0 | position col (signed word) |
| +0x31 | 0x5544b1 | order (signed byte; 2 = dead) |
| +0x168 | 0x5545e8 | engine frame (class 8) |
| +0x172 | 0x5545f2 | armor (the state handlers set it) |
| +0x173 | 0x5545f3 | death timer (signed; FUN_00484b89 counts it to 120) |
| +0x17a | 0x5545fa | formation slot |
| +0x180 | 0x554600 | shot windup (signed word) |
| +0x185 | 0x554605 | moving flag (== 1) |
| +0x18a / +0x18b / +0x198 | 0x55460a / b, 0x554618 | the movement state's step parameters |
| +0x194 | 0x554614 | blocked-ring flag (FUN_0046ebe4) |
| +0x197 / +0x19c | 0x554617, 0x55461c | troop band 0..3 / the band's step (FUN_00481abb) |
| +0x19a | 0x55461a | damage accumulator (FUN_0049459a) |
| +0x1a0 | 0x554620 | troops (signed word) |

## Functions

- **State handlers** FUN_004825d1 .. FUN_00482e6c (DAT_004d9140, class 0..10). States 0..6: FUN_00481abb
  (troop band) then +0x18a / +0x18b / +0x198 / armor +0x172 (0: 1/6/0/0, 1: 1/8/0/12, 2: 2/12/300/12,
  3: 3/12/100/35, 4: 2/30/0/35, 5: 1/6/0/0, 6: 4/16/200/25); state 0 alone also counts DAT_005694fc. States
  7..10: +0x19c = 0, then 0/20/0/33, 0/15/0/35, 0/30/0/50, 0/8/0/(25 with +0x13, else 40). Then the order
  +0x31 through DAT_004d9170 (partition `action`), the order a **signed** byte (`movsx`, `jl 0` / `jle 0x11`):
  out of range → FUN_0046ebe4 removes the unit.
- **FUN_00481abb** troop band: troops vs DAT_0053e86c / DAT_00522f54 / DAT_00522f5c (signed) → 0..3; +0x19c =
  word_4D98F8[4·class + band]. The only real return value of the partition (HOOK_R).
- **Thunks** FUN_004861e9 / f9 / 486209 / 19 / 29 / 39 → FUN_00486249 (walk), FUN_00486d83 (attack),
  FUN_004872ae (stand / turn), FUN_00487908 (order 9's), FUN_00487ce4 (dying), FUN_0048804a (missile shot).
  Each body sends class >= 7 to the engine sequencers FUN_00488793 (walk / stand), FUN_00488436 (attack),
  FUN_0048895e (dying).
- **FUN_0049459a** terrain attrition on a kind-10 / -17 cell: accumulator steps by DAT_00522f6c, a troop per
  100 / 160, at 0 troops a cue (FUN_004262cf 11 / 12 by formation slot) and order 2.

EAX: every function but FUN_00481abb returns an incidental register (a byte of the record, `432·u`); it flows
through the order handlers' tail calls to FUN_004822ed, which ignores it: HOOK_V, port returns 0.

## Quirks (retail behaviour a naive port misses)

1. **Stack residue in the engine sequencers.** FUN_00488793 / FUN_0048895e store their `[ebp-4]` into the frame
   +0x10 even when the class is not 7..10 (no case sets it), FUN_00488436 likewise (`[ebp-4]`, and `[ebp-8]`
   is set only for class 8). The bodies call them only for class >= 7, but the order handlers FUN_004842c6 /
   FUN_0048437e / FUN_004843bc / FUN_004849fc / FUN_00484a0c / FUN_00484b89 call them for EVERY unit: a foot
   unit's frame is then stack garbage. FUN_0048804a's `[ebp-0xc]` is unset for a view rotation DAT_00522f7c
   outside 0 / 2 / 4 / 6. Modelled as seams `bf_unit_res_*` (value in, final value out); the adapters move
   them with rh_res_get / rh_res_put. **When partition `action` ports those order handlers it must feed the
   seam** (the residue retail would have at that call: what the previous callee left there).
   The order handlers' residue model needs their ebp, so the driver and the state handlers carry their retail e0
   (`bf_unit_e0_4822ed` / `bf_unit_e0_state`, set by the E0 rows: driver e0 − 0x18 = state e0, state e0 − 0x18 =
   the order handler's ebp; bf-action.md quirk 14) — a docking siege tower (0x491492 removes it inside order 10's
   path step) gets that ebp's low byte as its dead record's frame.
2. **Dead wrap tests.** FUN_004872ae's turn-left (`dec` then `movzx; test; jge`) and FUN_0048895e's class-9
   phase (`dec` then the same) test a zero-extended byte for < 0: never true, so 0 steps to 0xff (FUN_0048895e
   then clamps >= 0x90 to 0x8f; FUN_004872ae keeps 0xff, `& 7` in the frame).
3. **Idle turn by column parity.** FUN_004872ae turns a standing unit's drawn direction +1 on an odd position
   col +0x20, -1 on an even one, and offsets the frame by the unit index & 7 (6 → 1, 7 → 2).
4. **Signed `% 8`** on facing / move direction (`cdq; xor; sub; and 7; xor; sub`): C's `%` on the signed byte.
5. **FUN_00487ce4 holds the last frame**: frame = base + death timer / 4 is stored only while that is <= 5.
6. FUN_00486249's phase tables are indexed by `phase & 0xfc` as a BYTE offset into dword tables (phase / 4).
   Its class-6 path indexes dword_4D9A28 by the phase WITHOUT stepping it (the step is on the moving path).

## Evidence

`verify --target wine --only bot3 --hooks '^bf_'` (2026-11-04): the 28 new rows, 23 reached, 21019723 calls /
10 scenarios, 0 divergences (nesting depths 2..5: the state handlers, then FUN_00481abb + the thunks + the
direct sequencer calls, then the bodies, then FUN_0048895e under FUN_00487ce4); hybrid PASS (11/11). The
residue rows compare their modelled local too (FUN_00488793 7364 calls from the order handlers, FUN_00488436
487, FUN_0048895e 81, FUN_0048804a 63423). Never reached in the corpus (c0, unverified): FUN_00482aed
(state 6), FUN_00482ca8 / FUN_00482d8a (states 8 / 9), FUN_00486219 / FUN_00487908 (order 9's sequencer).
In the hybrid pass the port's FUN_004822ed calls the port handlers directly (their rows count 0 replace
calls); the order handlers run as retail code on the port's C stack.
