# rl mpmsg3 — the MP message layer's remaining builders / receivers, 15 more delayed-action handlers (2026-11-14)

Partition `mpmsg3` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`): 83 functions — 33 payload
builders and their 33 receivers (send table 0x4d57f0 / receive table 0x4d5980, ids below), 15 delayed-action
handlers (table 0x4d5c98), the DirectPlay system-event logger 0x43e6a2 and its session fallback 0x43e8f4.
`src/rl_mpmsg3.c`, rows `tools/rhost/rh_hooks_rl_mpmsg3.c`, globals `src/rl_mpmsg3_globals.{h,c}` (generated). All 83
ported; the pending file is empty. The protocol (0x43eda0 → builder, 0x43eac1 → receiver, 0x447136 → 0x4472ce →
handler, the due tick 0x446e21): `rl-mpmsg2.md`. Transport / scenarios: `multiplayer-rhost.md`.

## Messages ↔ builders / receivers / actions

Fields in send order; "due" = 0x446e21(DAT_00544094) (4 B, or packed 3 + 1 with the player through DAT_00512f9c).
Both sides file the same action with the same due tick (builder for us, receiver for the sender).

| id | builder | receiver | payload (bytes) | effect |
|---:|---|---|---|---|
| 13 | 0x44255f | 0x442579 | player count DAT_0053e8a4 4 | receiver: title setup defaults 0x4ae539, clamp 0x4ae5e2, 0x433cc1, tabs dirty |
| 17 | 0x442add | 0x442ae8 | none | receiver: DAT_00568984 = 0 |
| 23 | 0x442d00 | 0x442d60 | due 4, save name DAT_0053f660 16 | action 3 (write the MP save) |
| 24 | 0x442dad | 0x442e0d | as 23 | action 2 (load the MP save) |
| 25 | 0x442e5a | 0x442eec | due 4 (no action) | both: session → screen 46, 0x42c64f, DAT_0052af94 = 2; else 0x46f21f |
| 28 | 0x443043 | 0x443083 | DAT_0059154c 4, load result DAT_0059151c 4 | receiver: DAT_00553d30[sender] = result, 2 counts DAT_0057a0dc |
| 29 | 0x4430d5 | 0x443143 | save name 16 | both: extension sva / svb; receiver loads (0x4adfe6) → DAT_0059151c 1 / 2, message 28 |
| 33 | 0x44334e | 0x44338b | DAT_0057c93c 1, DAT_005530b8 1 | both: 0x43a997 |
| 36 | 0x44356d | 0x443587 | local player 4 | receiver: the sender leaves (realm +4 = 0, aux +0x26 = 1) |
| 37 | 0x4435ce | 0x44363b | DAT_0057c93c 1, DAT_0053e9a0 1, DAT_0053e9ec 1, DAT_0057c988 2 | both: 0x439b52(…, b − 1, b − 1, w) |
| 45 | 0x443e51 | 0x443f09 | due, player, DAT_00553074, DAT_0053f080, DAT_00568d98 (4 each), troop row 128 | action 18 |
| 46 | 0x443fab | 0x44400f | due, DAT_0053e904 | action 19 |
| 50 | 0x444346 | 0x4443aa | due, DAT_0059154c | action 23 |
| 54 | 0x44462f | 0x4446a6 | due, siege attacker DAT_0055cd58, region DAT_0052f0a8 | action 26 |
| 55 | 0x44470a | 0x44475c | besieger DAT_00568ed0, DAT_00544090, DAT_00567d08 | both: 0x43b7c4 at once |
| 56 | 0x4447ab | 0x44480f | due, besieger | action 36 |
| 57 | 0x444860 | 0x4448c4 | due, besieger | action 37 |
| 61 | 0x444b07 | 0x444b17 | none | both: AUTO_RESOLVE 0x43b622 |
| 62 | 0x444b27 | 0x444bd0 | due, player, DAT_005653f4 | both: unpause; action 39 |
| 63 | 0x444c66 | 0x444cb5 | none | both: unpause; battle (DAT_0057c8fc == 2) → its end 0x43be65 |
| 66 | 0x444e3f | 0x444f96 | due + player packed 4, 10-byte row DAT_0053e930[p] | builder first clears battle hex DAT_0056d5d0's bits; action 33 + the row into record +0x2c |
| 67 | 0x445063 | 0x4451cc | packed 4, row 10, 2, 2, 2, 1, 1, 2, 1 | action 34 (+ row); receiver drops a repeated sequence (quirk 3) |
| 69 | 0x445458 | 0x4454a9 | due | action 38 |
| 71 | 0x44562e | 0x4456c1 | due, partner DAT_0057c8b8, player, answer DAT_0056d678 | yes → action 41; receiver: partner us → event 256 / 257 |
| 72 | 0x4457a1 | 0x445851 | due, player, DAT_00553fbc, DAT_005651cc, DAT_0057a0f8, DAT_005530b0 | action 42 |
| 75 | 0x445d97 | 0x445e0e | due, DAT_0059154c, DAT_0056d5cc | action 44 |
| 82 | 0x4460f8 | 0x446171 | player, territory DAT_00553efc, its +360 4 / +364 22 | receiver: into territory DAT_00553f34 |
| 83 | 0x4461f4 | 0x44622c | player, DAT_0059154c, DAT_00591550 (as a kind) | receiver: DAT_00591558 = 1, kind 1..4 → 0x43d929 / 0x43d9cd / 0x43dc1d / 0x43dd83 |
| 85 | 0x44637e | 0x446389 | none | receiver: ours == 1 → messages 86, 82; DAT_005533e0[sender] = 1 |
| 86 | 0x4463d9 | 0x4464e4 | the skirmish setup: 13 dwords, 2 aux bytes, 65-byte DAT_0053f5fc | receiver: 0x42d40b; DAT_0055cd34 → message 94, else the 65 bytes, 0x42d4aa; 0x498dcb, redraw |
| 87 | 0x446683 | 0x44678d | aux bytes 0x553da4 / 0x553dd0, DAT_0053e8fc, DAT_0053ef5c | DAT_005533e0 states (host 0 / 2), chat lines cleared, DAT_005533d0 = 1 |
| 95 | 0x446ba0 | 0x446bba | DAT_0056899c 4 | receiver: aux bytes = 2 |
| 96 | 0x446bec | 0x446c1f | aux bytes 0x553da4 / 0x553dd0 | receiver: 0x42bf46 |

| action type | handler | does |
|---:|---|---|
| 2 | 0x447a59 | load the MP save (DAT_0053f660 with "sva" / "svb", 0x4adfe6) |
| 3 | 0x447ac5 | write it (0x4ade93) |
| 18 | 0x447d72 | 0x437fd7(+4, +8, +0xc, +0x10) |
| 19 | 0x447d9d | disband occupant +4 (0x438681) |
| 23 | 0x447e0b | toggle option +4: DAT_0053f25c / DAT_0053f260 (+ every realm's provision 0x44df5f / plan 0x4485a5) / DAT_0053f264 / DAT_0053f284 |
| 26 | 0x447f82 | move occupant +4 to realm +8 (0x437535) |
| 33 | 0x448076 | player +4's row from the record, 0x478987, DAT_0053e9ac = 1, our row back |
| 34 | 0x448137 | the same + 0x478f0b, 0x481b9a, unit 0x479e0c: 0x4891ad, 0x479e90(unit, +8..+0x18) |
| 36 | 0x44825d | break occupant +4's siege link (0x43b917) |
| 37 | 0x448276 | 0x43b98a(+4) |
| 38 | 0x44828f | 0x496b9f |
| 39 | 0x44829f | 0x43bbd7(+4, +8) → the battlefield's end 0x43be65 |
| 41 | 0x4482e9 | alliance 0x4a1774(+4, +8) |
| 42 | 0x448308 | diplomacy action 0x4a2621(+4..+0x14) |
| 44 | 0x4483d1 | treaty cleared 0x4a1a54(+4, +8) |

0x43e6a2(kind, dpid): logs "OK :DP netsys - …" (strings 0x4d5d70..) for kinds 0..8; kind 2 (player deleted) and 4
(host) act (the function comment in `src/rl_mpmsg3.c`). Callers: 0x4b8c3a's DirectPlay receive loop (rl_io's port,
now calling rl_43e6a2 by name).

## Rows

All HOOK_V (no caller reads EAX); builders / receivers / handlers are table-only (entry-patched), 0x43e6a2 /
0x43e8f4 are E8-called. The port side reaches the builders / receivers through rl_mpmsg.c's k_mp_build / k_mp_recv
(33 ids added each); handlers still through 0x4472ce's engine_retail_call on retail's entries.

Callees: rl_ / bf_ names (other partitions' stubs where pending: 0x42d40b, 0x479e0c, 0x4b743b); old ports with
rows for 0x43b622 / 0x43be65 / 0x49cf20 / 0x437535 (through rl_evq_load / rl_evq_store), 0x438681, 0x44df5f,
0x4485a5, 0x43b917, 0x4a2621, 0x4ae539, 0x4ae5e2; TODO(rl-link): 0x49bb9d (in no partition) through engine_retail_call.

## Quirks (retail ground truth)

1. 0x43e6a2 kind 2 in the battle (DAT_0052af94 > 2) does not check 0x43e98b's result: an unknown DPID resigns slot 0
   and sets slot 0's aux +0x26 / shield slot.
2. 0x444b27 / 0x444bd0 / 0x444c66 / 0x444cb5 "unpause" a non-zero DAT_0053f238 with `not` (= ~it), not a store of 0:
   only -1 clears; a flag of 1 would become 0xfffffffe (still non-zero).
3. Receiver 67 (0x4451cc) keeps the last sequence per sender in the signed word DAT_00553100[sender]; an equal
   sequence is dropped (no action); a jump > 1 counts DAT_004d2158; kind 1 / 2 count DAT_004d2150 / DAT_004d2154.
   Builder 67 files its action only when DAT_00591554 == 0 but sends the message regardless.
4. Builders 25 / 61 / 63 and receiver 25 file no action: 0x444b07 / 0x444b17 resolve the battle and 0x444c66 /
   0x444cb5 end it at once on each side (not lockstep through the action table); message 25's due tick is read
   into DAT_00542cc0 and unused.
5. Receiver 86 (0x4464e4) reads builder 86's last dword (DAT_005440bc) into DAT_005691e4, and with DAT_0055cd34 set
   runs 0x498dcb and the redraw after sending message 94 without taking the 65-byte name block; otherwise the
   redraw 0x42b9c4 / 0x42ba40 / 0x42bf46 runs twice and DAT_0057d340 = 2 is set twice.
6. 0x446389 / 0x4464e4 / 0x446683 / 0x44678d index DAT_005533e0 (2 dwords) by player with no bound (as rl-mpmsg2
   quirk 3): player 2 writes DAT_005533e8.
7. Action 23 toggles 1..3 with "== 1 ? 0 : 1" (any other value → 1) but 4 (DAT_0053f284) with "== 0 ? 1 : 0".
8. Message 25's builder sets DAT_0057c90c = 46, its receiver does not.

## Evidence

- `verify --target wine -j 3 --only '^mp-' --hooks '^rl_(<the 83>)$'` (25 scenario sides: mp-start / play / war / duel /
  dp / leave / trio / skirmish / siege / five; 2026-11-14): **83/83 reached, 0 divergences**, 511 shadowed calls
  (2 skipped by the nesting pass: one each of 0x444cb5 / 0x44678d, shadowed in their other calls), depth 1 except
  0x443043 / 0x44356d / 0x4463d9 (2); **hybrid PASS** (all 83 replaced, `corpus --check` on all 25). Most called:
  0x44828f 46, 0x44475c 32, 0x446171 24, 0x445458 / 0x4454a9 23.
- Earlier iteration (`--only '^mp-(trio|skirmish|siege)'`): 66 of the 83 reached there, 0 divergences, hybrid PASS;
  the other 17 only in mp-dp / leave / five / war / start.
- Unverified: none of the 83 (every function is reached). Arms likely unexercised (per-arm coverage not measured; 0x43e6a2 ran 3 times): 0x43e6a2 kinds 5..8 (logs only) and kind 2
  outside the battle with ≥ 2 players left; 0x4451cc's repeated-sequence drop; 0x447e0b's options other than those
  the corpus toggles; 0x4464e4's DAT_0055cd34 arm; 0x44622c's kinds the corpus does not send.
