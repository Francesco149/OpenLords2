# Retail-layout port, partition `host` (2026-11-04, round 3)

`src/rl_host.c` (globals `src/rl_host_globals.{h,c}`, generated + widened by hand), rows
`tools/rhost/rh_hooks_rl_host.c`; design `battlefield-sim-port.md` (family `rl`). The 0x4a57ac.. band: the AI's army
orders, the siege board, the strategic move-confirm, spawn / mercenary helpers, the unit sweeps of the upkeep pass,
the load / save pump, save write / load, the game-init resets, the io path helpers (`io_read_file_at` …), the input
state, the frame clocks, the palette, the window procedure, the frame driver 0x4b99c0, the per-screen refresh
0x4ba26e, the menu / panel input 0x4ba9c8. All 70 ported, all 70 with rows.

## Routes

Round 3: every function had an old port, none on retail's layout under rhost.

- **(a) wrap** — the old port is linked in rhost and works on retail's layout (verified as a whole under an older row):
  `rl_<va>` forwards to it (its `static` dropped; retail's EAX modelled where the old port returned void).
  engine_army_ai.c (under `ai_run_movement` 0x4a5667): 0x4a58f4, 0x4a5b1f, 0x4a5f0a, 0x4a6270, 0x4a64ca, 0x4a65a3,
  0x4a6735. engine_combat.c (under `combat_spawn_unit` / `combat_begin_siege` / `combat_rotate_merc_offers`):
  0x4a7e0a, 0x4a7eb5, 0x4a9eb1, 0x4a9f18, 0x4a9f72, 0x4ac7f3, 0x4acb54. engine_realm_upkeep.c (under
  `realm_pay_upkeep`): 0x4ad0e8, 0x4ad16c, 0x4ad230, 0x4ad316, 0x4ad39a. gfx_sprite_rle.c: 0x4b495a.
  engine_battlefield_render.c: 0x4bc020.
- **(b) re-port** — the rest (49): the old port takes the event-queue struct (0x4a57ac, 0x4a60b9, 0x4ace5e), a
  context struct (0x4aabd8), or lives in a module rhost does not link (engine_army_move_confirm.c, engine_army_select.c,
  game_init.c, save.c, engine_load_pump.c, io.c, io_path.c, engine_input.c, engine_delta.c, engine_fps.c,
  engine_rate.c, engine_phase.c, gfx.c, main.c, engine.c, engine_menu_input.c, engine_diplomacy_action.c).

## Rows

All HOOK_R where retail's EAX is defined on every reached path (modelled: e.g. 0x4a65a3's `region << 8`, 0x4a8426's
`84 × army`, 0x4ae310's 75), HOOK_V where a path returns the caller's EAX (0x4b1867, 0x4b35c1, 0x4b3768, 0x4a9252)
or nobody reads it. Special rows:

- **0x4b99c0, the frame driver**: rhost's frame loop (`frame_wrap`) calls it, its only E8 site is redirected to the
  loop, so the row's install finds no call site and patches the entry. RH_SH_MEDIA | RH_SH_SAMPLE (per screen: the
  first 64 frames and every power-of-two-th): a shadowed frame compares the whole state and every surface pixel.
  It plays the movies (0x42dbc7 decodes a frame per call): `HOOK_SMK_V` (rh_hook_smk.h) puts smackw32's pages and
  the .smk file positions back before the port side — without it boot-movies diverged at f4 (the movie palette
  DAT_004ea1b0, 80640 px: the port decoded the next frame) and on the dirty box of every later movie frame.
  Its battle branch calls the per-unit driver 0x4822ed port-to-port, and bf_4822ed models a siege order's stack
  residue from its RETAIL entry esp (engine_bf_unit.c `bf_unit_e0_4822ed`, normally set by its own hook adapter): the
  row's adapter keeps 0x4b99c0's e0 (`rl_host_e0_4b99c0`, `HOOK_SMK_V_E0`) and rl_4b99c0 sets it to e0 − 0x1c (push
  ebp, 8 B of locals, ebx / esi / edi, the call) before the call. Without it every shadowed frame matched but the
  hybrid of field-orders ended on another state; `--shadow-all` on field-orders found the 2 frames of 17752 (f8789,
  f9601: a unit's +0x10, the removed unit's sprite frame, 0x88 vs 0xd0) — sampled out in the verify passes.
- **0x4b29be, the window procedure**: `__stdcall`, reached through the window class (entry patch): its own entry stub
  ends `ret $0x10` (`HOOK_R_STD16`). RH_SH_HOST (master adf8b06: never shadowed, the hybrid is its evidence) — its
  WM_DESTROY / WM_ACTIVATEAPP arms release / re-create the host objects. Before the flag existed it was shadowed:
  87314 calls over 60 scenarios, 0 divergences (1 skipped: an arena growth), the WM_DESTROY of game-quit included.
- **0x4bc020** takes 11 arguments: rh_port_run copies ten, so its body keeps e0 and the adapter reads the 11th at
  e0 + 44 (`HOOK_R11`; retail's side only reads its arguments).
- **0x4ba26e, 0x4ba9c8**: RH_SH_MEDIA (their callees draw); 0x4ba26e also sampled (every frame). 0x4ba9c8: a click
  can open a movie (movie-skip f662: lom.smk from the title menu). The port side's SmackOpen got another block
  (DAT_004d4d58 0x190002c0 vs 0x19000900): smackw32's spans come from rh_smack's bump pointer `g_stop`, which no shadow
  rolls back; under rh_hook_smk.h's page rewind (HOOK_SMK) smackw32 then crashed (an AV in 0x18008a3f). So the row
  (`HOOK_R_NOMOVIES`) is host-level in a `--movies` run (RH_SH_HOST for that run: retail in the shadow passes, the
  hybrid proves it) and shadowed otherwise. A shared fix: rh_smack's g_stop into the rh_hook_smk snapshot.
- **0x4b495a**: RH_SH_SAMPLE (a hot leaf of the RLE blits).
- **0x4a9372 / 0x4a960f / 0x4a975d / 0x4a983b**: confirm-dialog callbacks — 0x4a9252 passes their VAs to the dialog
  0x40e6f2 (retail keeps the VA), so they are entry-patched.

- **0x4b284e / 0x4b2922** create the DirectDraw object and the surfaces (WinMain's init, the options' restart, the
  wndproc's re-activation): RH_SH_HOST. (Shadowed before the flag: 60 / 60 calls, every one "skipped" — the creation
  grows the heap arena, rh_state compares nothing then — so the hybrid was already their only evidence.)

## Evidence

Final (the committed rows): `rhost.py verify --target wine -j 3 --hooks '^rl_(<the 70>)$'` over the whole corpus
(64 scenarios, master 5a18dce merged; 2026-11-05 00:04–00:53): **VERIFY: 70/70 reached with 0 divergences, 0 never
called; hybrid PASS; 0 failed runs** — 13.25 M calls shadowed (state + pixels where flagged), 9.6 M more sampled out
(RH_SH_SAMPLE rows, the RH_SH_HOST rows, rl_4ba9c8 under --movies); passes 1–3 + the hybrid (all 70 replaced,
`corpus --check`: every state hash and presented frame). The run before it (22:18–23:17, the same rows but 0x4ba9c8
shadowed in the movie runs too) had its one divergence there (movie-skip, the SMACK handle above). The three
RH_SH_HOST rows (0x4b284e, 0x4b2922, 0x4b29be) ran retail in the shadow passes (sampled 64 / 64 / 89928) and are
proven by the hybrid. Seams (commit b8bd0c2): every rl_ / bf_ row at once (687) replaced, `corpus --check` over the 64
scenarios: PASS. Least reached:
0x4b3768 (1 scenario), 0x4aabd8 (2), 0x4af62c / 0x4af675 / 0x4adfe6 / 0x4bc020 (3), 0x4ad316 (4), 0x4a5f0a / 0x4a960f /
0x4ad9f0 (5), 0x4a7e0a / 0x4a7eb5 / 0x4a8426 / 0x4ad0e8 / 0x4ad16c (6). Never exercised (no corpus path; ported from the asm): the MP
arms (0x4b99c0's link cadence, 0x4ad9f0's MP load / save, 0x4bb978's MP pause, the move-confirm's MP messages), the
debug keys of 0x4b29be (F3..F9, F12: DAT_0053f684), 0x4b284e's surface-failure path (rhost's DirectDraw does not fail).

## Quirks (retail ground truth)

1. 0x4b495a: the opcode lives in EAX across a row (`xor eax, eax` per row, `mov al, [esi]` per opcode): a literal
   opcode 0xff would leave 0x100 in EAX and every later opcode of that row reads 0x100 + byte. The encoder emits 1..16;
   the port models it.
2. 0x4bbc80: DAT_005aeb28 (the /13 pulse) is set but never cleared by the phase ticks (the other pulses are cleared
   every call).
3. 0x4bb8c0: the GOG build's run gate returns 1 after reading the CD flags (its checks are nopped).
4. 0x4aef7e: DAT_005c9a74 ends 1 on every path (the probe's "no CD audio" store was patched to 1 as well).
5. 0x4ae310: the four blurbs are cut at their first control byte (< 0x20), everything after it zeroed.
6. 0x4a7eb5: race 7's siege-type dword (DAT_004d8af8 + 6 × 0xf0) reads 0x03000003 — no lane matches, as for 3.
7. 0x4ba26e / 0x4ba9c8: both test the screen byte in an if-chain (re-read per case, no call between): one read is
   equivalent. 0x4ba9c8 page 9: a click outside the option list returns to page DAT_00553e5c.
8. 0x4a98fe ignores its first argument (the local player); the merge target +0x152 takes the low byte of the 5th.
9. 0x4a9372 reads the army's men, the keep cap DAT_004d8a0c[level] (= DAT_004d8a10[level − 1]) and the defender once,
   before any event post.
10. 0x4b29be WM_MOUSEMOVE stores the position as two words (DAT_004eabbe / c0); Esc returns 0 without DefWindowProc;
    WM_PAINT returns 1.
11. 0x4ade93 / 0x4adfe6: the save blocks are DAT_004de960's (address, size) pairs — retail ADDRESSES in .data: the
    standalone needs its own table.

## Lessons

- A sampled row's shadow passes are not proof: the frame driver matched on every shadowed frame (≈ 56k) but its
  hybrid failed one scenario — `--shadow-all` on that scenario finds the frames. A port that calls a bf_ / rl_
  function port-to-port must also feed that function's retail-stack seams (bf_unit_e0_4822ed …) itself.
- A movie-playing caller needs the smackw32 rollback (rh_hook_smk.h) even when the movie call is deep inside it.

## Seams

- TODO(rl-link) (a linked port on a struct / its own binder): 0x437535 (engine_combat_transfer_army: an event-queue
  argument), 0x40cfd2 (engine_button_render_list: rl_render's reason), 0x411fde, 0x41635f, 0x418fc5, 0x419243,
  0x4190db, 0x4181eb, 0x41eaa3, 0x41ece6, 0x4149ec, 0x41f321, 0x41f1dd, 0x41f01f, 0x41f98b, 0x41f86d, 0x41f5e8 (the
  screens' binders), 0x4357a6 (merchant zone), 0x4650b0 (engine_army_state_run), 0x477dfc (battle end tick: an
  event-queue argument), 0x4050c0 (the map scene tick), 0x4bc1d1 / 0x4bc142 (the battlefield frame / scroll),
  0x40f7d3 / 0x41a734 (the HUD panels), 0x423b4f / 0x41f15a (the MP HUD), 0x476e95 (the tooltip).
- TODO(rl-extern) (unported, in no partition): 0x4a599d, 0x4ae539, 0x41f4a1, 0x4bbf89, 0x4bbfd2, 0x4247f5,
  0x4b26b8, 0x42e50d, 0x42e3ba, 0x4b2730 (and rl_42eaa6 / rl_42dfe2 / rl_425d35 / rl_4b7180: quit, by name).
- Ported 2026-11-14: 0x43f7e3 (F8: reload old_turn.sv[ab]) + 0x43ff63 / 0x446f06 / 0x43ff08 / 0x43ffc3 (its clears),
  rows in rh_hooks_rl_host.c; g_rl_569530 widened to [60] (byte_569530[6][10]).
- Replaced in the done partitions (one-line seams → rl_<va>): rl_glue (0x4b1deb, 0x4b18e3, 0x4a8426, 0x4af502),
  rl_skirmish / rl_render / rl_ui2 (0x4af502, 0x4af8d9, 0x4b25b0, 0x4b0ab5, 0x4b1867), rl_ui3 (0x4af8d9, 0x4a9f72),
  rl_io (0x4b0ab5, 0x4b1867, 0x4ae310 ×2, 0x4af54a, 0x4af502), engine_bf_extern (0x4af8d9, 0x4af502, 0x4b25b0),
  engine_bf_input (0x4b1deb). Left: rl_glue's two-line 0x4af8d9 calls (rl_40ae12), rl_ui3's 0x4b2922, and
  engine_bf_setup.c's 0x4af502 / 0x4af8d9 calls (the file does not include rl.h).
- Other partitions by name: rl_472bc5 / rl_472ae0 / rl_46f68e / rl_46f700 / rl_46ff43 / rl_4701ac / rl_470678 /
  rl_4707be / rl_46f2ae / rl_46d2a3 / rl_472e46 (terr), rl_401136 / rl_404dc1 / rl_404d6b / rl_40da1e / rl_40e3ee /
  rl_40e5e7 / rl_40e6f2 / rl_40ccfa / rl_4017bf / rl_4020ae / rl_401f49 / rl_40210c / rl_401246 / rl_42f1df /
  rl_42e5c0 / rl_42ec91 / rl_42eb7c / rl_427d19 / rl_40f1a0 / rl_419c78 / rl_429ba4 / rl_429b1d / rl_42a15f /
  rl_40490d / rl_418e2d / rl_41aea2 / the editor keys (ui4), rl_43de70 / rl_45270a / rl_45240a / rl_4527a6 /
  rl_4329ec / rl_432b05 / rl_433a23 / rl_433ce4 / rl_43582a / rl_42ff10 / rl_4472ce / rl_451f1a / rl_452160 (ctl),
  rl_4976a1 / rl_499a1c / rl_499034 / rl_4984dc / rl_49a010 / rl_476aa7 / rl_47ed34 (turn); the done partitions'
  rl_ / bf_ functions.

## Globals (hand edits in rl_host_globals)

Widened for the standalone (exact under rhost either way): DAT_004e6910 (the getcwd scratch, 100 B), DAT_004ead60 /
DAT_0053f660 (the load / save names, 16 B), DAT_00500c30 (the local player's path-cost field, 0x1000 i16). Not
widened (shared-file needs): rl_glue's DAT_0053f2b8 is 0xc8 B but 0x4ae310 writes 4 × 200 B; the button-record /
hotspot tables 0x4dd310.. are 2-byte stubs in rl_host_globals (only their address is taken: the standalone needs the
records). rl_glue_globals.h conflicts with rl_io_globals.h / rl_render_globals.h (0x4ea130 / 0x4dd640 declared with
different types), so rl_host.c declares the rl_glue globals it reads locally.
