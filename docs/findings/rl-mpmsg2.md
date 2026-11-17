# rl mpmsg2 — the MP message layer's second half, on retail's layout (2026-11-14)

Partition `mpmsg2` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`): 58 functions, the payload
builders / receivers 0x4431ef..0x4462df, the delayed-action machinery (0x446e21, 0x446f84, 0x447026, 0x447136,
0x4476e4, 0x447740) and ten delayed-action handlers. `src/rl_mpmsg2.c`, rows `tools/rhost/rh_hooks_rl_mpmsg2.c`,
globals `src/rl_mpmsg2_globals.{h,c}` (generated). No old port (the pre-rhost `engine_handlers_da.c` /
`engine_delayed_action.c` model the dispatcher on a port struct, not retail's layout). Transport and scenarios:
`multiplayer-rhost.md`.

## The protocol pieces

- **Send**: 0x43eda0(id, dest) (glue) points DAT_00512f8c at ring slot DAT_00512fb0 + 260·k + 4 and calls the
  builder DAT_004d57f0[id]; a builder appends fields with 0x4401ba(ptr, n) (mpmsg). The size table DAT_004d5b90[id]
  fixes the payload length; the slot header is (id, size, checksum 0x440038, slot | retry marks).
- **Receive**: 0x43eac1 (mpmsg) sets DAT_00515918 = DAT_00515c50 + 4 and calls the receiver **DAT_004d5980[id]**
  (IDA's funcs_43ED8E); a receiver takes the fields in the builder's order with 0x4401f6(ptr, n).
- **Delayed actions**: 0x447136(type, due, a2..a11) files a record in the 400 × 56 B table DAT_0050d7c0 (+0 due,
  +4..+0x28 the ten args, +0x36 live, +0x37 type; the free record from 0x4476e4 → DAT_0051591c, DAT_00515b24 =
  it, DAT_00512f6c / DAT_00512fa0 = the live count from 0x447740). 0x4472ce (ctl) runs a record once due: args into
  DAT_005158e8 (+4), DAT_005158a8 (+8), DAT_005158a4 (+0xc), DAT_005158b0 (+0x10), DAT_005158ac (+0x14) …, then the
  handler **DAT_004d5c98[type]** (50 entries).
- **The due tick**: 0x446e21(lead) = lead + max DAT_0053e8d0[i] over players alive (realm +5), human (+4), not out
  (aux +0x26); in the battle only DAT_005681ec / DAT_005681d8. Builders use lead DAT_00544094 (50 after 0x446f84),
  message 30 lead + 10, message 47 a fixed 25. Both sides file the same record with the same due tick: the builder
  for the local player, the receiver for the sender — lockstep.
- **Lead statistics**: 0x447026 rings the last 5 leads (due − DAT_0057c998) in DAT_00515890 (cursor DAT_00515884,
  count DAT_00512fa4) and from the fifth on stores the average in DAT_00515960[local]; a due tick already past goes
  to 0x4470c4 instead ("ERR: putting todo"; unless type 27 the late count / sum / average DAT_0051587c /
  DAT_00512f54 / DAT_00512f50, and message 90 outside the battle). 0x446f84 resets all of it.

## Messages ↔ builders / receivers / actions

| id | builder | receiver | payload (bytes) | effect |
|---:|---|---|---|---|
| 30 | 0x4431ef | 0x443286 | due 3 + player 1 packed in DAT_00512f9c, phase DAT_0057c934 1 | action 1 |
| 34 | 0x4433d9 | 0x44341b | county DAT_005681e0 1, cell DAT_0056795c 4, type DAT_0059154c 1 | receiver: a field's use 0x438bec |
| 35 | 0x44348c | 0x4434eb | county 1, its byte +0xb9 1 (packed) | receiver: the byte, income 0x44b80b |
| 39 | 0x443748 | 0x4437aa | county 1, tax DAT_00553534 1 (packed) | both: tax 0x43933b |
| 40 | 0x44381d | 0x4438c3 | the trade (1, 4, 1, 4, 4, player 1, county 1) | both: 0x4284ce |
| 41 | 0x443990 | 0x443a2d | player 1, due 4, county DAT_0057c8c4 1, DAT_0056d5ac 2, DAT_0056d5a8 2 | action 12 |
| 42 | 0x443ad5 | 0x443b8d | due, player, county, merc DAT_0055446c, cost DAT_00565400 (4 each), troop row DAT_0053f6a0[32 p] 128 | action 13 |
| 43 | 0x443c2f | 0x443ca6 | due, county, tier DAT_0056d898 (4 each) | action 16 |
| 44 | 0x443d0a | 0x443dba | message 41 at 4 bytes each + DAT_00553f44 | action 12 (sixth arg) |
| 47 | 0x444060 | 0x444100 | due (lead 25), player, county, target DAT_00553c78 (4 each), caravan row DAT_005678c0[6 p] 24 | action 20 |
| 48 | 0x444191 | 0x44419c | none | receiver: outside the battle with DAT_005533d0 == 1 → message 51 |
| 49 | 0x4441d7 | 0x444280 | county, DAT_0059154c, DAT_00591550, county +0x15e (1 each, packed) | both: rations 0x43a5a9 |
| 51 | 0x4443fb | 0x444415 | the 42-byte block DAT_00515920 | receiver (battle, screen ≠ 43): 0x47f937; DAT_0059154c == 1 → screen 43, DAT_00568470 = 5000 |
| 59 | 0x4449a0 | 0x4449f1 | due 4 | action 49 |
| 60 | 0x444a2f | 0x444a9b | DAT_005653f8 4, DAT_0057cae0 48, DAT_0056d6b0 4, DAT_0053f654 4 | both: DAT_0053f018 = 0, the field 0x4778a0, log "OK :Bat taken sent / get" |
| 64 | 0x444d04 | 0x444d68 | player, due (4 each) | action 30 |
| 73 | 0x4458e5 | 0x445960 | player, DAT_00553fbc, DAT_005651cc, DAT_0057a0f8, DAT_005530b0 (4 each), record DAT_00567d10[202 p] 202 | receiver: event 245..257 (0x472bc5) for the target; the target us → DAT_0059154c, kind 0 → msg 74, kind 4 → msg 75 |
| 74 | 0x445c99 | 0x445d23 | due, DAT_0059154c, DAT_0056d5cc, DAT_00543fcc (4 each) | action 43 |
| 76 | 0x445e6f | 0x445ea7 | map DAT_0053f034, DAT_0053e90c, DAT_0057cacc (4 each) | receiver: the MP setup's map (0x46a037, 0x467770, 0x4298c1(0), 0x4ae5e2, 0x433cc1) |
| 77 | 0x445f43 | 0x445f80 | shield slots DAT_0057cb40 6, realm DAT_0056d5cc's +0x0a 1 | receiver: the local realm's shield, human, title page 6 |
| 84 | 0x4462c5 | 0x4462df | DAT_0057a0f0 4 | receiver (client, page 6): 0x433551; non-zero → DAT_005533e0[local] = 1, msg 85 |

| action type | handler | does |
|---:|---|---|
| 0 | 0x4479de | nothing |
| 1 | 0x4479e9 | phase (+8) equal to ours → realm (+4)'s +0 = 999, the ranking cookie, DAT_005530d0 (quirk 1) |
| 7 | 0x447b8e | DAT_0056d6a4 = 1, DAT_0052afe4 = timeGetTime, message 8, Sleep 50, message 8 |
| 12 | 0x447c68 | order an army: 0x4a98fe(+0x10, +4, +8, +0xc, +0x14) |
| 13 | 0x447c99 | spawn a unit 0x4a9a9a(+4, +8, +0xc, +0x10); DAT_005530d0 = 1 |
| 16 | 0x447d22 | build the keep 0x436d02(+4, +8) |
| 20 | 0x447db6 | the shipment 0x43b145(+4, +8, +0xc) |
| 30 | 0x44800e | charge 0x47a76d(+4) |
| 43 | 0x448339 | move +0xc of realm +8's +0x118 to realm +4 (if it has it, signed); realm +4's +0x8c + 16·(+8) = the amount |
| 49 | 0x448422 | the rankings 0x435211; DAT_0057d340 = 2 |

## Rows

Builders, receivers and handlers have no E8 call site (only the three tables): entry-patched, HOOK_V — 0x43eda0 drops
the builder's EAX, 0x43eac1 returns the receiver's to 0x4b8c3a which drops it, 0x4472ce drops the handler's. 0x4472ce
(ctl) still calls the handlers through engine_retail_call; the patched entries run the rows. HOOK_R for the directly
called six (EAX modelled; 0x447136's is read by nobody in the corpus but is defined: -1 or the live count).
0x447136 has 12 arguments: HOOK_R12 notes e0 in the body (saved / restored) and the adapter reads the last two at
e0 + 44 / 48 (rh_port_run copies ten). 0x4479e9: rh_res_get / rh_res_put(8); 0x448339: rh_res_set(8 / 0xc / 0x10).

Globals: the partition's own (generated) are aliased by its table; four of partition mpmsg's (DAT_00512f9c,
DAT_00515960, DAT_00542cc0, DAT_00553e70) are aliased here too until mpmsg's table emits RH_RL_MPMSG_ALIASES (ld
accepts the same absolute symbol twice at the same value; a PE weak external cannot carry an absolute default — tried).
Other partitions' globals headers clash when included together (conflicting types for the same symbol: g_rl_4dd640,
g_rl_5c9270, g_rl_4ea130, g_rl_53f288, g_rl_57cb40 between rl_render / rl_io and others): rl_mpmsg2.c declares the
ones it needs itself. The ranking cookie DAT_0056d6a0 is a mirror (engine_army_ai): set through
engine_army_ai_set_ranking_dirty.

Callees: by rl_ / bf_ name (0x4401ba / 0x4401f6 / 0x401136 / 0x43eda0 / 0x472bc5 / 0x43a5a9 / 0x438bec / 0x43933b /
0x46a037 / 0x467770 / 0x4298c1 / 0x433cc1 / 0x49caaa / 0x4afab9 / 0x4778a0 / 0x47f937 / 0x433551 / 0x4a98fe /
0x47a76d / 0x435211); verified old ports with rows for 0x4284ce, 0x44b80b, 0x4a9a9a, 0x436d02, 0x43b145, 0x4ae5e2;
TODO(rl-link): 0x4470c4 (in no partition) through engine_retail_call.

## Quirks (retail ground truth)

1. 0x4479e9 compares an **uninitialized local** ([ebp-4], e0 − 8: what 0x4472ce's previous callee left there) with the
   local player — equal → DAT_0055403c = 2 — and only then stores realm DAT_005158e8 in it. Modelled as
   rl_4479e9_res; the dispatcher's port models the slot across a pass (engine-quirks #241).
2. 0x4462df compares DAT_005c9a74 with 2 and then jumps unconditionally over the arm that would set title page 10
   when DAT_0057a0f0 ≠ 0: dead code.
3. 0x4462df writes DAT_005533e0[local] with no bound: the array is 2 dwords (0x5533e0..0x5533e8); local player 2
   writes 0x5533e8 (the army-state cursor DAT_005533e8).
4. Builder 77 (0x445f43) sends realm DAT_0056d5cc's shield byte (+0x0a) but receiver 77 (0x445f80) stores it into the
   LOCAL realm's +0x0a (and its aux +0x25).
5. Builders 30 / 35 / 39 / 49 pack 1-byte fields (and message 30 a 3-byte due tick) through the 4-byte scratch
   DAT_00512f9c (0x401136 copies); receivers unpack into zeroed dwords, so a due tick ≥ 2^24 or a county ≥ 256 would
   truncate.
6. Messages 41 / 44 carry the same action 12 with different field widths (1 / 2 bytes vs 4).
7. 0x447026's average is a signed idiv by 5 of the five leads (truncates toward zero).

## Evidence

- `verify --target wine -j 3 --only '^mp-' --hooks '^rl_(<the 58>)$'` (8 scenarios: mp-start / mp-play / mp-duel /
  mp-war, both sides; 2026-11-14): **58/58 reached, 0 divergences**, 3781 shadowed calls, depth 1 except 0x445c99
  (depth 2: inside 0x445960); **hybrid PASS** (all 58 replaced, `corpus --check` on all 8, mp-play included). Most
  called: 0x447026 / 0x4476e4 / 0x447740 674, 0x447136 447, 0x4479de 388, 0x446e21 225, 0x4479e9 130 (quirk 1's
  residue modelled: rh_res_put compared, 0 divergences).
- Earlier iteration (`--only '^mp-(start|duel|play)' --hooks '^rl_44[3-8]'`): 32 of the 58 reached there (the rest
  only in mp-war), 0 divergences, hybrid PASS.
- Unverified: none of the 58 (every function is reached). Unexercised arms: 0x447136's full table (-1) and late path
  (0x4470c4: a due tick not after the current one); 0x445960's kinds other than those mp-play / mp-war send;
  0x444415's screen-43 early return; 0x4462df with a non-zero DAT_0057a0f0 for local player >= 2.
