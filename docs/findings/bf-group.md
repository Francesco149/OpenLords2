# Battlefield port, partition `group` — the group AI, projectiles, fire (2026-11-04)

Partition `group` of the battlefield port (`battlefield-sim-port.md`): 42 functions, all ported in
`src/engine_bf_group.c` (no stubs left in `engine_bf_group_pending.c`), each a row in
`tools/rhost/rh_hooks_bf_group.c`. Verified under rhost (wine) over the 11 `bot3*` scenarios (10 fight on the
field): shadow (retail vs port on identical state) + the hybrid replace pass. Evidence per batch: the commit
messages. Older pre-rhost RE: `combat-battlefield-ai.md` §1b–§1c / §3 (corrections below).

## The combat cluster (frame driver FUN_004b99c0, sub-state DAT_0057c8fc == 2)

`0x478952` marker countdown → `0x40490d` → **`0x488dfe` aggregator** → `0x4822ed` unit driver → **`0x485bb1`
projectiles** → **`0x489401` group AI** → **`0x4859e5` fire** → `0x477dfc` battle end.

| function | what |
|---|---|
| `FUN_00488dfe` aggregator | phase 2 only. Live groups: count +2 / owner +0 / first member +4 cleared; cooldown +0xc runs down, else the hit counter +0x12; +0xf, +0xe run down; +0x2d runs down and zeroes +0xf; melee flag +0xd cleared. Then units re-bucket (+0x178 group): first / last member, count, owner; a unit's hit latch +0x15 → group hit counter +1, cooldown 50, attacker group +0xa (from the attacker unit +0x16 via DAT_0056d5d0); order 4 → +0xd, order 9 → +0x11. DAT_0057c8dc = live groups. |
| `FUN_00485bb1` projectiles | `FUN_0046ef3c` (clear every live record's cell byte +6 and +4), then per record (0x57a100, 1..100): `FUN_00492c8b` flight step, then by kind +9: 4 frame table DAT_004d9950[life >> 1] + `FUN_0046efbe` + the +0x31 toggle; 5 frame table DAT_004d99a8[life >> 4] (+ `FUN_0046efbe` when > 0), at life 2 the cell kind becomes +0x3e (7 → kind 5 + cell +1/+4/+3 cleared, 15 → kind 0 + +1/+3 cleared); 7 a blast on the cell + 4 neighbours (`FUN_00485675`); else `FUN_0046efbe`. Life +0x3c (signed) runs down, <= 0 frees (`FUN_0046eb78`). No RNG call of its own (its callees may). |
| `FUN_004859e5` fire | only while DAT_0053e9d0 > 0: kind 16 → 17 everywhere (re-arms the flag), then a kind-15 cell with a kind-17 neighbour (`FUN_00496f72`) ignites (`FUN_00485861`). The sweep cursor is retail's globals DAT_00569570 / DAT_00569578 / DAT_005533cc. Never active in the corpus (no fire in bot3). |
| `FUN_00489401` driver | every 101st call `FUN_0047fc01`; per live group (loop index DAT_00568458 = the handlers' "current group"): `FUN_004891ad`, then (group +1 clear) the stance +8 through DAT_004d91b8 (interactive, DAT_0053e8fc == 0; 0..4) / DAT_004d91f8 (auto, 0..10) / DAT_004d91d0 (auto with group +3 set, 0..8); then re-target countdown +0x14: <= 0 → 500 and, when `FUN_00489654`, `FUN_0048970e`. |

## Formation / re-target chain

`FUN_0048970e`: fits at the anchor (`FUN_00489f9d`) → formation layout (`FUN_00489896`); stance 6 with
DAT_00553fb0 > 6 → all to the anchor (`FUN_00489981`, never reached in the corpus); else member by member onto
enemy cells (`FUN_00489798` → `FUN_0048a38e` + `FUN_00489a62` nearest member + `FUN_00489b8d` order). Clears +0x13.

- `FUN_0048a1c9` layout: spacing DAT_00568224 / columns DAT_0056d8a8 from the group's top-priority class
  (`FUN_004819cc`: DAT_004d98c8[cls] priority, DAT_004d96d4 / DAT_004d96d8 per class, stride 0x14); group +9 == 1
  → 2 columns; no more columns than members. Origin DAT_0057c970 / DAT_00567968 (centred on the anchor, clamped);
  DAT_005530d4 / DAT_00542cc4 are the far edges BEFORE the clamp.
- `FUN_00489f9d`: also leaves the anchor cell's facts for the order issuer — DAT_00553fe4 = (kind == 2),
  DAT_005681fc = occupant, DAT_005440d0 = terrain (+4).
- `FUN_00489b8d` order issuer: foot → order 3 (9 on a kind-2 anchor; class 6 keeps its order there), engines →
  10 (a "tired" +0x13 one keeps its order unless the group asked to re-form); missile weapon 1..2 with a target
  → 17, 3+ on terrain 1..3 → 12 (group waypoint +0x16 / +0x18); a unit holding order 9 keeps it.

DAT_00567968 lives inside `g_caravan_order` (retail 0x5678c0, the economy's staging array: row 7) — accessed via
that owner symbol (`G_567968` in engine_bf_group.c).

## Interactive stances (DAT_004d91b8) and the stance helpers

Every 200th tick per group (+0x1c), never while a member is in melee (+0xd); +0x1a is the step counter.
DAT_004eea98 (`FUN_0047fc01`: weighted troops, percent(side 0, side 1) - 100, + RNG jitter (DAT_0058fd7c & 31) - 10
outside MP) against DAT_0057c8b4 picks the "stronger" branch.

- stance 1 `FUN_0048a9d2`: stronger — strike back (`FUN_0048c974`) when hit by a live group, else by step: hold
  (0..9), advance halfway (`FUN_0048c56f`, 10..17), regroup (`FUN_0048b386`, 18..20), advance. Weaker — pursuit
  DAT_00553070 → advance; hit → strike back at the attacker; recall DAT_0056d89c → `FUN_0048b7fc`; every 4th step
  objective 0; hit > 10 times → withdraw from the attacker (`FUN_0048b5fc`).
- stances 2 `FUN_0048acd2` / 3, 4 `FUN_0048b02b` (one body in the port, `bf_stance_hold`): stronger — onto the
  attacker (`FUN_0048c4bd`), charge (`FUN_0048c8af`) from step 10 / 13, else waypoints (`FUN_0048b9c1`). Weaker —
  a hit raises DAT_00553f28; past the side's limit DAT_00553080[DAT_005653f8] the pursuit level DAT_00553070 += 3,
  supply DAT_00553f74 / 8 > DAT_0056d6ac → += 20, else recall + withdraw; any enemy group (>= 3) within 8 / 9 or
  pursuit → charge; else an objective (`FUN_0048bac4` 2 / 1) on a step cadence.

The helpers (also called by partition `stance`) set the current group's anchor +0x22 / +0x24 and order it there
(`FUN_00479e90(g, x, y, 0, 0, 0)`); FUN_0048ef45 = nearest enemy group by Chebyshev centre distance.

## Quirks (retail behaviour a naive port misses)

1. **`FUN_00489b8d`'s target is the anchor cell's occupant**, DAT_005681fc as `FUN_00489f9d` left it — also on
   the enemy-cell path (`FUN_00489798`), where the order's cell came from `FUN_0048a38e`: the missile order 17
   targets whoever stood on the group's anchor at the fit check (0 when it is the unit's own side).
2. **`FUN_0048a38e` scans full squares, not rings**: radius i = 0..19 re-scans every inner cell; the visited list
   DAT_004ee900[100] (cleared per `FUN_00489798`) dedups. DAT_0057c970 / DAT_00567968 are written for every
   accepted cell, even one already listed (the search then continues) — the globals can hold a rejected cell.
3. **`FUN_0048b7fc` steps TOWARD the remembered point** DAT_00522f70 / DAT_00522f74 (the attacker's centre when
   the recall was armed); `FUN_0048b5fc` steps AWAY from the attacker group. `combat-battlefield-ai.md` §3 calls
   0x48b7fc "withdraw (moves AWAY)": wrong — 0x48b5fc withdraws, 0x48b7fc returns.
4. **The pursuit level DAT_00553070 is one global** for every AI group: one group's hits raise it for all; any
   group's "weaker" tick decays it by 1; a group's rout flag +0x2a clears only when it is <= 0.
5. **The re-target countdown re-arms (500) even when the group may not re-form** (`FUN_00489654` false).
6. `FUN_0048bbe2` tries 17 slots for 16 (the start slot twice); with none set it ends on start + 1, minus (1, 2).
7. `FUN_00485bb1` kind-7 blast strengths are asymmetric: cell / row above 32(n-1) + 40, row below + 0, right + 25,
   left + 10.
8. `FUN_00488dfe`: the hit counter +0x12 decays only while the cooldown +0xc is 0; a hit re-arms +0xc = 50.
9. Several helpers write their own argument slots (`FUN_00489b8d` x / y, `FUN_0048bbe2` column, `FUN_0048c363` /
   `FUN_0048c3fe` the offset) — invisible to callers (no caller re-reads them); not modelled.
10. Every handler re-reads the current group DAT_00568458 at each use; the port's `CG` macro does too.

## Seams

- `FUN_00404ead` / `FUN_00404f4c` → `engine_path_dist_manhattan` / `engine_path_dist_chebyshev` (engine_path,
  linked; its |dx| / |dy| globals are rhost mirrors of DAT_00591560 / DAT_0058fea0 — read them as
  `g_path_dist_dx / dy`, never `g_bf_591560`). `FUN_00404dc1` → `engine_math_percent_div`. RNG → `bf_rng_step()`.
- Callees in other partitions by name: strike (`bf_492c8b`, `bf_46efbe`, `bf_485675`, `bf_46eb78`, `bf_496f72`,
  `bf_485861`, `bf_4956cc`), action (`bf_479e90`, `bf_48a672`), stance (`bf_496566`, `bf_496d76`, the DAT_004d91f8 /
  DAT_004d91d0 handlers).
- No retail VA is called directly; no TODO(bf-link) / TODO(bf-extern).

## Coverage (bot3*, 14 scenarios, wine, 2026-11-04)

Shadow calls with retail callers (verify `--hooks` on the inner rows, so each is shadowed where retail's own
callers reach it): the drivers 0x489401 / 0x488dfe / 0x485bb1 / 0x4859e5 / 0x46ef3c ~184k each; 0x4891ad 739k
(11 scenarios before the merge); 0x48acd2 307910, 0x48a9d2 92153, 0x48b02b 37355; 0x489a62 10029,
0x489b8d 9992, 0x48a38e 3547, 0x48a1c9 / 0x4819cc 2513, 0x489f9d / 0x48970e 1536, 0x48ef45 1215,
0x48b9c1 330, 0x48b37b 252, 0x48c8af 227, 0x48bac4 200, 0x48c56f 122, 0x48bbe2 115, 0x47fc01 1391,
0x48bff9 / 0x48c363 27; one call each: 0x48b386, 0x48b5fc, 0x48c4bd. All 0 divergences.

Never called in the corpus (ported from the disasm, hybrid-replaced but never executed — UNVERIFIED):
`FUN_00489981`, `FUN_0048a9c7` (empty), `FUN_0048b546`, `FUN_0048b7fc`, `FUN_0048bcfb`, `FUN_0048becd`,
`FUN_0048c06f`, `FUN_0048c3fe`, `FUN_0048c974`. The fire pass body (DAT_0053e9d0 > 0) never runs either.
