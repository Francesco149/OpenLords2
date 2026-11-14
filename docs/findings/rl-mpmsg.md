# rl mpmsg — the MP message layer's first half, on retail's layout (2026-11-14)

Partition `mpmsg` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`; transport:
`multiplayer-rhost.md`): 54 functions (0x43e330..0x442cd6) the mp-* scenarios newly reach. `src/rl_mpmsg.c`, rows
`tools/rhost/rh_hooks_rl_mpmsg.c`, globals `src/rl_mpmsg_globals.{h,c}` (generated; `g_rl_567964` hand-added).
All fresh ports (route b); the four with old pre-rhost ports (engine_rate.c's tick minima, engine_state_hash.c's
`engine_compute_state_hash`) keep those untouched — they are not on retail's layout.

## The message format

A message is `{id, size, checksum, seq | copy}` + `size` payload bytes. `size` = the id's entry in 0x4d5b90 (255 =
undefined id). checksum = 0x440038: the byte sum of the payload (header excluded). Byte 3: the send ring's slot
DAT_00512f4c (1..39, low 6 bits) | 0x40 (2nd copy) | 0x80 (3rd copy). 0x43eda0 (rl_glue) builds into the ring
0x512fb0 (40 × 260 B) through the builder table 0x4d57f0[id] with DAT_00512f8c at the payload; every id except
10 / 89 / 67 / 68 / 66 / 39 / 49 / 35 / 14 / 84 / 29 goes out three times (seq, seq | 0x40, seq | 0x80).
The receive pump 0x4b8c3a puts one message at 0x515c50 and calls the dispatcher 0x43eac1(from DPID): the sender's
slot (0x43e98b: session +0 = DPID), the duplicate filter (resend countdowns 0x515b30, 7 × 41 bytes: a (slot, seq)
seen less than 10 messages ago is dropped), the checks, then the receiver table 0x4d5980[id] with DAT_00515918 at
the payload (0x4401f6 reads it, 0x4401ba writes builders' bytes).

## Ids of this partition (builder / receiver)

| id | size | builder | receiver | what |
|---:|---:|---|---|---|
| 1 | 44 | 0x441066 | 0x4410bd | join: DPID, name (32), shield, machine score |
| 2 | 57 | 0x44121a | 0x4412cc | the creator's welcome: DPID, name, both slots, slow-link flag, both scores, joined byte |
| 3 | 151 | 0x4413a4 | 0x44164d | the roster (count, realms' +0x0a / +5, sessions' +0x25 / DPID / name, 0x57cb40, 0x5530e0) |
| 4 | 8 | 0x441905 | 0x4419f9 | start the game (DAT_0058fe28 / 08) |
| 5 | 40 | 0x441b4e | 0x441b86 | a shield choice (slot, shield, name) |
| 6 | 69 | 0x441d2a | 0x441e1f | a chat line (slot, 65 B) |
| 7 | 4 | 0x441f2a | 0x441f50 | joined (slot) |
| 8 | 16 | 0x441f76 | 0x442014 | end of turn: tick stamp, 0x515960 (2), the 10 hash sums; reported flag 0x515d60 |
| 9 | 52 | 0x4420ad | 0x4421c3 | the game options (12 dwords 0x53f288..) + player count |
| 10 | 4 | 0x442309 | 0x442361 | a tick stamp (single copy) |
| 11 | 4 | 0x4423b9 | 0x44240a | 0x447136(0, 0x446e21(DAT_00544094)) |
| 12 | 16 | 0x442448 | 0x4424d6 | the hash mid-turn (8 without the reported flag) |
| 14 | 51 | 0x4425bf | 0x4427d1 | the roster on the setup page (single copy) |
| 15 | 8 | 0x4429f2 | 0x442a25 | who I am: slot, DPID |
| 18 | 0 | 0x442afd (empty) | 0x442b08 | reload the old turn (0x43f7e3) |
| 20 | 4 | 0x442bad | 0x442c01 | 0x447136(7, 0x446e21(..) + 50) |
| 21 | 4 | 0x442c3f | 0x442c82 | toggle DAT_0053f238 (+ redraw marks) |
| 22 | 1 | 0x442cb9 | 0x442cd6 | one byte DAT_0059151c → 0x49b973 |

Ids 0 / 98 / 99 are the empty pair 0x441050 / 0x44105b (size 255: undefined). Ids 13, 16, 17, 19 fall in this range
but belong to no partition (0x44255f / 0x442579, 0x442a7f / 0x442aae, 0x442add / 0x442ae8, 0x442b18 / 0x442b80).

The port side's tables: `rl_mpmsg_build(id)` / `rl_mpmsg_recv(id)` (rl_mpmsg.c) call every builder / receiver that
has an rl_ name (this partition, mpmsg2's), else retail's through engine_retail_call. rl_43eda0 (rl_glue) now builds
through `rl_mpmsg_build` and checksums through rl_440038; rl_43eac1 dispatches through `rl_mpmsg_recv`.

## Rows

HOOK_R where a caller reads EAX: the tick minima 0x43e330 / 0x43e433 / 0x43e50d (0x43dece.., 0x424069), 0x43e98b,
0x440038 / 0x44015a (char returns: EAX modelled exactly, AL over the loop counter's final value), 0x4401ba /
0x4401f6 (the counter), 0x4407e5 (0x49b8e6), 0x440dd2 (0x4406ff), 0x43e5c1 (exact though unread). HOOK_V for the
rest: 0x43eac1's EAX is dropped by 0x4b8fed; builders' / receivers' EAX goes back through 0x43eda0 / 0x43eac1 and
is dropped; 0x43fd2a is __usercall (EAX in comes back out when MP is off) and nobody reads it. The 36 builders /
receivers have no call site: entry-patched.

## Quirks (retail ground truth)

1. The tick minima 0x43e330 / 0x43e50d / 0x43e433 loop p = 1..6 over the 6-record realm / session tables: slot 6 reads
   past them (0x57bf00 + 6 × 352, 0x553d50 + 6 × 44).
2. 0x440dd2 on the strategic map compares every live human's sum [k] against a local byte ([ebp-0xc]) it never sets on
   that path: stack residue (modelled: `rl_440dd2_res`, e0 - 0x10). On the battlefield the first ally's sum is the
   reference.
3. 0x43eac1: a message from an unknown DPID uses session slot 0's countdown row; slot 6's countdown is reset to 0
   right after being set to 10; the seq indexes 41-byte rows with values up to 63 (spills into the next row).
4. 0x43eac1's checksum compare goes through `movzx ax, al` / `movsx eax, word`: an unsigned byte compare.
5. 0x440231 (the state hash) forces sum [7] = 1 after the battle group pass has summed into it, so the group records
   never reach sums [0] / [7]; slot [9] = (DAT_0057c998 & 0x7f) + 1, never 0 (0x440dd2's "0 = not reported" test).
6. 0x4413a4 clears sessions 0..5's +0x14; 0x4425bf (the same roster) does not.
7. The tick stamp (8 / 10 / 12) packs the tick's low 3 bytes + our slot byte; the receiver copies them into the low
   bytes of DAT_00542cc0 / DAT_0056d5cc (0x401136): their upper bytes keep what they held.

## Evidence

- `verify --target wine -j 3 --only '^mp-' --hooks '<the 54 rows>'` (8 scenarios: mp-start / mp-play / mp-war / mp-duel,
  both sides, 2026-11-14): **54/54 reached, 0 divergences, hybrid PASS** (all 54 replaced, `corpus --check` on the 8),
  0 failed runs. Calls (depth): 0x43e330 276824 (1), 0x43e433 287251 (1), 0x43e50d 116977 (1, mp-war only),
  0x43e5c1 4 (2), 0x43e98b 24480 (2), 0x43eac1 24462 (1; 2 skipped), 0x43f01d 9, 0x43f4b7 10, 0x43f8ff 312,
  0x43fd2a 322, 0x43fed2 9, 0x440038 22739, 0x44015a 95704 (3), 0x4401ba 583, 0x4401f6 624, 0x440231 390,
  0x4407e5 1620, 0x440dd2 312 (the battle path: mp-war; the residue model in / out), builders / receivers 1..22: 1 to
  21792 calls each (0x441d2a / 0x441e1f chat: 2; 0x442afd / 0x442b08: 1).
- 0x442b08 (the old-turn reload: save_load allocates) is "skipped" by the shadow (the arena's commit changes during the
  call: not compared) — its evidence is the hybrid pass only; the same 2 nested calls are 0x43eac1's skipped ones.
- First run caught two bugs, both fixed: the roster of id 14 carries no names (51 B, not 151); and
  `engine_retail_call` took at most 8 arguments, so the 12-argument stub of 0x447136 (mpmsg2's, called by receivers 11
  / 20) stored stack garbage into the delayed-action table 0x50d7c0 (+0x24 / +0x28): engine_retail_call and rhost's
  binding (rh_hooks_bf.c `retail_call`) now pass up to 12.
- `make -C src`, `make -C tests run` (2295 passed, 0 failed) build.

Unverified: none reached-wise. Paths not exercised: 0x43eac1's four error branches (corrupt / wrong size / bad CRC /
forced resend), 0x4410bd's refusal (message 81) and the in-game ignore, 0x441b86's taken-shield path (message 17),
0x4419f9's "game started without us", 0x440dd2's strategic path (the residue reference), 0x43fd2a / 0x43f4b7 with MP off.
