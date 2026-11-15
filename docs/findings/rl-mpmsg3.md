# rl mpmsg3 — the MP message layer's remaining builders / receivers, 15 more delayed-action handlers (2026-11-14)

Partition `mpmsg3` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`): 91 functions — 37 payload
builders and their 37 receivers (send table 0x4d57f0 / receive table 0x4d5980, ids below), 15 delayed-action
handlers (table 0x4d5c98), the DirectPlay system-event logger 0x43e6a2 and its session fallback 0x43e8f4.
`src/rl_mpmsg3.c`, rows `tools/rhost/rh_hooks_rl_mpmsg3.c`, globals `src/rl_mpmsg3_globals.{h,c}` (generated). All 91
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
| 38 | 0x4436bb | 0x4436f9 | county DAT_0057c93c 1, category DAT_0053f0d8 1 | both: 0x439edb(county, category − 1) at once (the workforce grid's double-click; mp-labor, 2026-11-15) |
| 45 | 0x443e51 | 0x443f09 | due, player, DAT_00553074, DAT_0053f080, DAT_00568d98 (4 each), troop row 128 | action 18 |
| 46 | 0x443fab | 0x44400f | due, DAT_0053e904 | action 19 |
| 50 | 0x444346 | 0x4443aa | due, DAT_0059154c | action 23 |
| 54 | 0x44462f | 0x4446a6 | due, siege attacker DAT_0055cd58, region DAT_0052f0a8 | action 26 |
| 55 | 0x44470a | 0x44475c | besieger DAT_00568ed0, DAT_00544090, DAT_00567d08 | both: 0x43b7c4 at once |
| 56 | 0x4447ab | 0x44480f | due, besieger | action 36 |
| 57 | 0x444860 | 0x4448c4 | due, besieger | action 37 |
| 58 | 0x444915 | 0x444948 | county DAT_0057c93c 1, task DAT_0055416c 1 | sender: 0x43d309 already ran (0x43ce1a); receiver: 0x43d309(them); both DAT_0052afcc = 1 (mp-labor) |
| 61 | 0x444b07 | 0x444b17 | none | both: AUTO_RESOLVE 0x43b622 |
| 62 | 0x444b27 | 0x444bd0 | due, player, DAT_005653f4 | both: unpause; action 39 |
| 63 | 0x444c66 | 0x444cb5 | none | both: unpause; battle (DAT_0057c8fc == 2) → its end 0x43be65 |
| 66 | 0x444e3f | 0x444f96 | due + player packed 4, 10-byte row DAT_0053e930[p] | builder first clears battle hex DAT_0056d5d0's bits; action 33 + the row into record +0x2c |
| 67 | 0x445063 | 0x4451cc | packed 4, row 10, 2, 2, 2, 1, 1, 2, 1 | action 34 (+ row); receiver drops a repeated sequence (quirk 3) |
| 69 | 0x445458 | 0x4454a9 | due | action 38 |
| 70 | 0x4454e7 | 0x4455ad | due, the ally (4 bytes from realm +0x81), player, county DAT_0055ce50, price DAT_00567958 | action 40 (the type-10 popup's yes, 0x4367ff; mp-price) |
| 71 | 0x44562e | 0x4456c1 | due, partner DAT_0057c8b8, player, answer DAT_0056d678 | yes → action 41; receiver: partner us → event 256 / 257 |
| 72 | 0x4457a1 | 0x445851 | due, player, DAT_00553fbc, DAT_005651cc, DAT_0057a0f8, DAT_005530b0 | action 42 |
| 75 | 0x445d97 | 0x445e0e | due, DAT_0059154c, DAT_0056d5cc | action 44 |
| 82 | 0x4460f8 | 0x446171 | player, territory DAT_00553efc, its +360 4 / +364 22 | receiver: into territory DAT_00553f34 |
| 83 | 0x4461f4 | 0x44622c | player, DAT_0059154c, DAT_00591550 (as a kind) | receiver: DAT_00591558 = 1, kind 1..4 → 0x43d929 / 0x43d9cd / 0x43dc1d / 0x43dd83 |
| 85 | 0x44637e | 0x446389 | none | receiver: ours == 1 → messages 86, 82; DAT_005533e0[sender] = 1 |
| 86 | 0x4463d9 | 0x4464e4 | the skirmish setup: 13 dwords, 2 aux bytes, 65-byte DAT_0053f5fc | receiver: 0x42d40b; DAT_0055cd34 → message 94, else the 65 bytes, 0x42d4aa; 0x498dcb, redraw |
| 87 | 0x446683 | 0x44678d | aux bytes 0x553da4 / 0x553dd0, DAT_0053e8fc, DAT_0053ef5c | DAT_005533e0 states (host 0 / 2), chat lines cleared, DAT_005533d0 = 1 |
| 93 | 0x4469bd | 0x4469ec | the .skr name DAT_0053f5fc 65, DAT_005440bc 4 (→ DAT_005691e4) | receiver: 0x42d40b lists *.skr; found → mode DAT_0053e91c 3, the name, 0x42d4aa, redraw; else the fallback (DAT_0055cd34 1 and mode 3 → mode 0, redraw); message 94 back (mp-skrpick) |
| 94 | 0x446ab8 | 0x446b0f | DAT_0055cd34 4 | builder: the fallback; receiver: 0 → mode 3, 0x42d4aa, redraw, DAT_005530f0 = DAT_00553e5c; 1 and mode 3 → mode 0, redraw; DAT_0057d340 = 2 |
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
| 40 | 0x4482be | ai_diplomacy_gift 0x4a1b29(+4 ally, +8 payer, +0xc county, +0x10 price) |
| 41 | 0x4482e9 | alliance 0x4a1774(+4, +8) |
| 42 | 0x448308 | diplomacy action 0x4a2621(+4..+0x14) |
| 44 | 0x4483d1 | treaty cleared 0x4a1a54(+4, +8) |

0x43e6a2(kind, dpid): logs "OK :DP netsys - …" (strings 0x4d5d70..) for kinds 0..8; kind 2 (player deleted) and 4
(host) act (the function comment in `src/rl_mpmsg3.c`). Callers: 0x4b8c3a's DirectPlay receive loop (rl_io's port,
now calling rl_43e6a2 by name).

**19 / 26 are dead in this exe** (2026-11-15): their only send sites are the window procedure's F6 (VK 0x75: 19 if DAT_0053f684 != 0) and F7 (0x76: 26 if == 1); DAT_0053f684 is BSS and written only 0 (WinMain 0x40eaf9, 0x4975fb), in no save block, no pointer to it — the debug gate is compiled shut. The pairs are debug toggles: 19 = builder 0x442b18 flips byte_56D690 (rl_render's per-record checksum overlay) and "unpauses" DAT_0053f238 with `not`, receiver 0x442b80 copies both; 26 = builder 0x442f5f steps byte_553F4C 0→1→2→3→4→0 (read nowhere), receiver 0x442ff0 copies it.
**Resend forced is dead too**: 0x43eac1's DAT_0055ce68 arm — the dword is only decremented there, never set.

**Ids with no port (2026-11-15, after `mp-desync`; all ported since: §The last ids):** builders / receivers of 19, 26 (dead, above), 90 (a late action,
0x4470c4: unreachable through lag — retail's tick pacing, multiplayer-rhost.md §Desync). **81 is dead in this exe**: the
join's fourth dword (receiver 1 0x4410bd's "shield" test, 1 or 2 → DAT_005bb4a0) is DAT_005c9a74, which 0x4aef7e
leaves 1 on every path (rl-host.md quirk 4: the GOG build's patched CD check) — every joiner unlocks the 3+ player
session, the refusal never runs. 16 / 91 ported (mp-desync, §Resync below); 70 ported (mp-price, §Message 70 below).
**Dead** (no reference in the
exe: no call, no pointer): 52's / 53's senders 0x4a7948 / 0x4a7daf, 89's 0x43f939 / 0x43f9c3; no send site at all: 27,
31, 32, 65, 68, 78, 79, 80, 88, 92, 97 (98 / 99 / 0 = the empty pair 0x441050 / 0x44105b).

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
10. Receiver 97 (0x446d4e) files action 46 at DAT_00553e70 (the builder's last due tick), not at the due tick it
   just read into DAT_00542cc0; it copies the 3 + 1 packed bytes over DAT_00542cc0 / DAT_0056d5cc without zeroing
   them first (receiver 66 zeroes), so their high bytes survive.
11. Receiver 65's unpacking 0x479738 masks bits 0..8 (builder 0x479558's group +0x14) and drops them: +0x14 is never
    restored. The builder's 3-byte field keeps only bit 0 of +0x13 (bit 23); signed words can carry into the next field.
12. 0x43faa4 (receiver 89, DAT_0053e978 == 399) copies 0x8700 bytes of the other snapshot to 0x522da0 + 0x1b0 =
    0x522f50: 0x40 below the map table DAT_00522f90 and 0x6c0 past its end (over DAT_0052af94 …). rhost: identical
    (retail layout); the standalone writes outside g_overworld_map. 0x43faa4 / 0x43fd2a also read layout rows 4..6
    past the 0x30-byte DAT_004d5b10 block.
13. 0x469370 (receiver 80) indexes the army records DAT_0053f9b5[768 p] and the roster DAT_0053ea00[20 p] with the
    received realm unbounded; the map bytes by the roster's raw cell value.

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

## Resync (2026-11-15, `mp-desync`)

| fn | role |
|---|---|
| 0x43f5c5 | the snapshot restore (0x440666: the battlefield hash sums differ): see the function comment; sends 16 |
| 0x47ebe8 | the battlefield occupancy rebuilt after the restore (cell flags 0x80 cleared, units back on their cells, 0x49207e, 0x485bb1) |
| 0x442a7f / 0x442aae | builder / receiver 16: the player (ours / the sender's), its flag DAT_00515850[p] = 1, 0x43f855 |
| 0x43f855 | every live human player flagged → DAT_00553fa8 = 0, 0x43f8ff |
| 0x44695f / 0x446983 | builder / receiver 91 (no payload): DAT_00512f88 = 0, 0x43ff63, 0x446f06, 0x43f304 |

Quirk 9: 0x47ebe8's cell walk leaves DAT_00569578 /
DAT_00569570 at 80 / 80 and DAT_005533cc at 51200 (the port must write engine_map_query's g_cell_cursor_row / _col —
the one copy; a write to the g_bf_ alias is lost at the next mirror sync: the first verify's only divergence). The same for its DAT_0053e9ac = 1: the one copy is engine_territory_dirty_state()->below_status_redraw (rh_hooks_ai.c's s_dirty mirror) — through the alias the standalone lost the below-status redraw (state equal, every presented frame from f21273 different; the shadow passed); rl_444b17 / rl_448076 wrote it the same way, now the accessor.
Unexercised arms: 0x43f5c5 with no snapshot (returns 0), its strategic mode (no copies), the 4th resync in a row
(DAT_00553214 ≥ 4: last-turn reload / 0x4778a0, event 259).


## Message 70 (2026-11-15, `mp-price`)

0x4367ff (the type-10 popup's yes) sends 70 in MP instead of paying at once: builder 0x4454e7 / receiver 0x4455ad
file action 40 (0x4482be → ai_diplomacy_gift 0x4a1b29) on both sides with the same due tick. Reaching it takes an AI
ally whose average county population (realm +0x14) clears the personality bar (750 / 800 / 900 / 1000) and whose
attitude is ≥ 10, then a roll ≥ 2 × attitude (0x4a38db → 188). In MP the setup's County status *strong* gives every
AI > 1000 from the start; one compliment (+15) and an alliance offer (179) do the rest. Builder 70 sends 4 bytes from
realm +0x81 (the ally byte and the three after it); the receiver passes the whole dword to action 40 as the ally, the
builder's own action takes the byte — equal while +0x82..+0x84 are 0.
Evidence: rhost shadow 5 rows (0x4454e7 / 0x4455ad / 0x4482be + 0x4367ff / 0x4a1b29), mp-price 2/2: 9 calls, 0
divergences; replace: hybrid PASS; standalone 2/2 = pure retail; det 0 dwords; Windows ≡ wine.

## The last ids (2026-11-15)

Every id of the tables now has a port; k_mp_build / k_mp_recv cover 0..99 (the engine_retail_call fallback is dead).

| id | builder / receiver | payload, effect |
|---|---|---|
| 0, 98, 99 | 0x441050 / 0x44105b | empty |
| 31, 32, 78, 79, 92 | 0x443322 / 0x44332d, 0x443338 / 0x443343, 0x44603e / 0x446049, 0x446054 / 0x44605f, 0x4469a7 / 0x4469b2 | empty |
| 19 | 0x442b18 / 0x442b80 | DAT_0056d690 flipped 0 ↔ 1 (1), the pause DAT_0053f238 `not`-ed when set (4) |
| 26 | 0x442f5f / 0x442ff0 | DAT_00553f4c 1 → 2 → 3 → 4 → 0, else → 1 (1) |
| 27 | 0x44300a / 0x443024 | the event staging record DAT_00553510 (24); receiver publishes it (0x472df5, via rl_evq_load / store) |
| 52 | 0x444479 / 0x4444f0 | due, DAT_0055cd58, DAT_0052f0a8 → DAT_00569520 / DAT_00553234; action 24 |
| 53 | 0x444554 / 0x4445cb | due, DAT_00568ed0, DAT_00554000 → the same; action 25 |
| 65 | 0x444db9 / 0x444dfc | host only / guest only, battlefield: 2 groups packed (0x479558) into DAT_00544030 (7) → unpacked (0x479738) |
| 68 | 0x4453a3 / 0x445407 | player, due; action 35 |
| 80 | 0x44606a / 0x4460a1 | realm DAT_0059154c (4), its 20 roster cells' map bytes DAT_00554050 (60): gather 0x4692a7 / scatter 0x469370 |
| 81 | 0x4460d8 / 0x4460e3 | empty / DAT_0058fe10 = 1 |
| 88 | 0x4468a6 / 0x4468b6 | 0x43fd2a (no payload; IDA's EAX arg is bogus) |
| 89 | 0x4468c6 / 0x4468f3 | record DAT_0053e978 (4), 200 B at DAT_0056759c; receiver points it into our snapshot (0x43fa1d), compares (0x43faa4) |
| 90 | 0x44692a / 0x446935 | empty / the lead DAT_00544094 += 2 (slow link DAT_00553264) or 1 |
| 97 | 0x446c61 / 0x446d4e | packed due / player, the DAT_0053e930 row, DAT_0059154c (2); action 46 (quirk 10) |

Callees ported with them (no other caller): 0x479558, 0x479738, 0x4692a7, 0x469370 (one stack arg each of the last
two; IDA's __usercall EAX is scratch), 0x43fa1d, 0x43faa4. New globals: g_rl_522f80, g_rl_53e978, g_rl_554050[60],
g_rl_554404, g_rl_56759c, g_rl_56845c, g_rl_57c8a8 (rl_mpmsg3_globals); g_bf_544030 widened to [7]. Quirks 10–13.
Rows: 44, all HOOK_V (E8 callers of the six callees drop EAX). Static gaps (`prove_no_retail_code.py --gaps-only`):
107 → 69 (the 38 table entries closed).


**Forged (2026-11-15, `mp-forge1` / `mp-forge2`):** rh_net forges arriving id bytes so receivers no send site reaches
run on live payloads. Ported with them: action 24 0x447f44 (0x4a79a3 place garrison(+4, +8) via rl_evq_load / store),
action 46 0x4483fb (empty), and 0x4470c4 (the late-action path of 0x447136: logs "ERR: Putting todo packet too late",
unless type 27 the late statistics DAT_0051587c / DAT_00512f54 / DAT_00512f50 and, off the battlefield, message 90 →
lead += 1 / 2 on every peer); rl_447136 calls it by name (was an engine_retail_call seam). New global g_rl_4d603c (the
string). Static gaps 69 → 66.
Evidence: `verify --target wine --only '^mp-'` (39 sides) with the 47 rows: 10 reached (0x442b80, 0x442ff0, 0x443024,
0x4444f0, 0x44692a, 0x446935, 0x446d4e, 0x447f44, 0x4483fb, 0x4470c4), 13 calls, 0 divergences; hybrid PASS.
Unverified (never called): the other 37, incl. every builder but 90's and the callees of 65 / 80 / 89.
