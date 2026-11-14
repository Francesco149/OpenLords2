# rl mpnet — the SIGS transport, the monitor thread, the MP title pages / popup (2026-11-14)

Partition `mpnet` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`; MP under rhost:
`multiplayer-rhost.md`): 35 functions only the MP scenarios reach. `src/rl_mpnet.c`, rows
`tools/rhost/rh_hooks_rl_mpnet.c`, globals `src/rl_mpnet_globals.{h,c}` (generated). All 35 ported; the pending
file is empty.

## Routes

| VA | what | row |
|---|---|---|
| 0x4b61b0 | the ASIGSSession constructor (0x74 B zeroed) | TC |
| 0x4b62ea | Initialize(hwnd, dir, exe, game): load, the game-start event, the wait | TC |
| 0x4b63d0 | wait for the lobby's game start (hObject, 300 ms, message pump) | TC |
| 0x4b64d8 | SNWValidate + SierraNW load + InitializeDLL + CreateThread(0x4b7052) | TC |
| 0x4b6754 | resolve the 18 SierraNW exports into +44..+112 | TC |
| 0x4b6ae5 | game started? (CheckGameConnectStatus == 1 → +0) | TC (monitor only) |
| 0x4b6b71 | receive one message (CheckForAny → Peek → Recv per player) | TC |
| 0x4b6c59 / 0x4b6c82 | SendTCPMessage / SendTCPPointMessage | TC |
| 0x4b6ccb / 0x4b6cee / 0x4b6d19 | PingTime(2, -1) / TellMeSpecialChat / TellMePlayerLeft | TC |
| 0x4b6d44 | still connected? (CheckGameConnectStatus != -1 → +4) | TC |
| 0x4b6dc5 / 0x4b6de6 / 0x4b6e1b | GetNumberOfPlayers(0) / creator? / GetMyPlayerNumber | TC |
| 0x4b6e8e | GetGameOption into a 100 B buffer, strncpy out | TC |
| 0x4b7052 | the monitor thread StartAddress (a coroutine under rhost) | TC, entry patch, RH_SH_HOST |
| 0x4b7514 | MP: lock new players out (DirectPlay +124) + log | V |
| 0x4b7a9a | open / resume the SIGS session (title → Multiple players) | R |
| 0x4b7f78 | the provider dialog's DLGPROC (template 0x81) | R stdcall, entry patch, RH_SH_HOST |
| 0x4b90fb | send ring slot → DirectPlay Send or SIGS point / broadcast | V |
| 0x4bbf89 / 0x4bbfd2 | keep-alives: message 0x54 every 2 s / 0x0e every 1 s | R |
| 0x43306a | the local shield pick → aux +0x25, realm +0x0a, DAT_0057cb41 | V |
| 0x46f2f1 | rebuild the map's army links (cells' link bytes, territories +4, 0x46eddf) | V |
| 0x47f937 | apply a peer's battle result (42 B at DAT_00515920) | R |
| 0x49b973 | reset the turn run state; MP restart: reload lastturn / old_turn .sv[ab] | R |
| 0x49ca60 | seat taken by another? | R |
| 0x4b3d5d | copy n bytes (dword blocks, then bytes) | V |
| 0x433551 | the MP client's setup page 6 → page 11 (a load) / 8, the text field | V |
| 0x41f3e9 / 0x41f4a1 | the MP setup page's frame / the creator's game-type buttons | V, RH_SH_MEDIA |
| 0x41f77a | the custom / MP setup labels (+ MP lines 13..15 for the creator) | V, RH_SH_MEDIA |
| 0x475e07 | popup panel 12: the MP messages between humans (kind = event − 245) | V, RH_SH_MEDIA, residue |

Callees outside the partition stay retail by bare VA: the session's Close 0x4b6a15, 0x4b6ab6 and scalar deleting
destructor 0x4b92a0 (__thiscall: a cdecl adapter, as rl_io.c's nw_call), operator new 0x4c3200, _splitpath 0x4c2fb0,
sprintf 0x4c2a80, the DirectPlay connection 0x4b8243, the title setup's 0x41f98b / 0x41fbcb / 0x41f86d, 0x46eddf,
0x41ab67, 0x40cfd2 (TODO(rl-link) / TODO(rl-extern) lines). strncpy (pure) is the port's loop. Win32 goes through
retail's IAT slots under rhost (`IMP` in rl_mpnet.c, rl_io_plat.h's rule); the SierraNW exports through the pointers
retail stored in the session (rh_net's fixed thunks).

## Rows

- **Thiscall** (the session's methods): the session is the port's first argument (`self`). `HOOK_TC`: the entry stub
  saves ECX before RH_ENTRY's body uses it and returns `ret $N` (callee pops); the body makes it the row's `this`
  (saved / restored: methods nest); the retail side runs through a per-row thunk (hook_t.entry swapped on the first
  call, as rh_hooks_gfx.c's EAX row) that loads ECX and jumps to the original entry.
- **The monitor coroutine**: rh_net.c runs StartAddress on its own 256 KB stack (0x5e000000..0x5e040000); it calls
  0x4b6d44 / 0x4b6ae5. RH_ENTRY's 256 KB drop would leave that stack and a shadow there takes a rollback point inside
  the coroutine (rh_net: fatal). On that stack the stub bypasses the machinery: replace → the port in place (no mirror
  sync — the coroutine runs inside the main thread's Sleep / wait, possibly inside a port call); else → retail's entry.
  Those calls (and StartAddress itself) are proven by the hybrid pass only; in the hybrid, rl_4b7052 runs as the
  coroutine (1 call per scenario) and calls the method ports directly.
- **The DLGPROC** 0x4b7f78: RH_SH_HOST — rh_win.c's fake list box (g_lb / g_lbn / g_lbsel) is host state the shadow
  does not roll back: retail's LB_ADDSTRING leaves row 0, the port's add would get row 1 (wParam DAT_004e5a10 0 vs 1).
- HOOK_V where EAX is not defined / not read: 0x4b7514 (SP: the caller's EAX), 0x4b90fb (void), 0x43306a, 0x46f2f1,
  0x4b3d5d, 0x433551, the title pages, 0x475e07.

## Quirks (retail ground truth)

1. 0x475e07 reads its `[ebp-8]` (e0 − 12) before any store: the previous call's popup kind (it stores kind there
   later, the sender at e0 − 8). A residue of 3 (kind 3 = event 248) with a liege (+0x81 of the local realm) dismisses
   the popup (0x476768) instead of drawing it. Modelled: the row passes rh_res_get(12) and puts kind / sender; a
   port-to-port call (rl_47309e's case 12) has no retail frame and passes 0.
2. 0x4b6b71 polls players 0..GetNumberOfPlayers **inclusive** (one more than the count); a message larger than the
   buffer → DebugBreak and 0.
3. 0x4b90fb's 4th argument (0x43eda0 pushes reliable 0 / 1) is never read: DirectPlay always sends DPSEND_GUARANTEED (1);
   a SIGS point message goes to dpid − 1.
4. 0x4b7a9a: a resumed session only re-waits (0x4b63d0) and keeps its callbacks; -2 from Initialize exits the game
   (exit(0)). The creator keeps the session only when DAT_005c9a74 is 1..2, else DAT_0058fe10 = 1 and it is dropped.
   The session's +28 / +32 point at 0x4b7a9a's stack buffers (dangling after it returns; rh_net registers them
   volatile).
5. 0x4b7514 returns the caller's EAX in single player (no store); 0x43306a returns (44 × player & ~0xff) | shield.
6. 0x47f937 copies the peer's result with strutil_bounded_copy (src, dst, n — 0x401136's argument order) and returns
   its count (1).
7. 0x49b973: the MP restart reloads "lastturn.sv[ab]" the first time, "old_turn.sv[ab]" after (a = the creator), posts
   event 0x103 and sets the deadline DAT_005440c8 = DAT_0053f26c + 10 (a fresh turn: + 100, DAT_00553fc8 = 2).
8. StartAddress `retn`s without popping CreateThread's parameter (declared with none).

## Seams

- Replaced after verification: rl_glue.c rl_43eda0 sends with rl_4b90fb (was engine_retail_call); rl_io.c rl_4b8c3a
  calls rl_4b6d44 / rl_4b6e1b / rl_4b6b71 directly (nw_call keeps Close 0x4b6a15 and the delete 0x4b92a0).
- Left retail-by-VA on purpose: rl_ui2.c's mp_resume (0x4b7a9a) and rl_ctl.c's 0x4b7514 — 0x4b7a9a's wait pumps
  messages into the window procedure, so the call keeps engine_retail_call's mirror sync (rh_port_retail_enter /
  leave); replace mode patches the entries, so the ports run anyway. DLG(0x4b7f78) must stay retail's address
  (DialogBoxParamA takes the pointer).

## Second job: the old ports' deferred MP branches

- **diplo_seal** (engine_diplomacy_action_model.c, 0x436408): MP sends 0x43eda0(0x49 / 0x48, 0) by whether the partner
  is in the game (+0x05) instead of queueing — implemented (rl_43eda0). mp-play-p1 / -p2: 0 divergences, hybrid PASS.
- **trade_seal_confirm** (engine_trade_model.c, 0x435286): MP sends 0x43eda0(0x28, 0) instead of executing the
  trade — implemented (mp-war reaches it: 3 calls, 0 divergences).
- Still deferred, not reached by any row on MP state: engine_roster.c's 0x43eda0(0xf) (0x43e9e2, not a row; rl_ctl
  owns the retail-layout port), engine.c's engine_diplomacy_action_confirm (not linked into rhost). The all-rows
  verify over mp-duel and mp-war shows no other diverging row.

## Evidence

- mp-start / mp-play / mp-duel (6 scenarios), the 31 non-render rows: 0 divergences (0x4b6b71 / 0x4b6d44 107271
  calls each, 0x4b6c59 10658, 0x4b90fb 10676, 0x4bbf89 / 0x4bbfd2 1664, 0x46f2f1 22, 0x49b973 11 …), hybrid PASS; the
  4 render rows: 0x41f3e9 3, 0x41f4a1 901, 0x41f77a 56, 0x475e07 603 calls, 0 divergences, hybrid PASS; with the seams
  (rl_43eda0 10071, rl_4b8c3a 105932, diplo_seal 2): 0 divergences, hybrid PASS.
- mp-war-p1 / -p2, **every row** (1598; 1158 reached): 0 divergences, hybrid PASS (all replaced). Ours there:
  0x4b6b71 / 0x4b6d44 219050, 0x4b6dc5 13792, 0x4b6c59 13413, 0x4b90fb 13411, 0x475e07 2231, 0x4b3d5d 1630,
  0x4bbf89 / 0x4bbfd2 1030, 0x41f4a1 270, 0x47f937 5, 0x4b6c82 6, rl_43eda0 12141, rl_4b8c3a 208276, diplo_seal 6,
  trade_seal_confirm 3. One failed run: shadow pass 3 on mp-war-p2 ended with state_sem a8e39d14… ≠ the corpus
  d25671a6… (passes 1, 2, 4.. and the hybrid match). Not ours: mp-war-p2 with exactly this partition's + the seams'
  pass-3 rows shadowed ends at d25671a6 (corpus). Another pass-3 row (1094 of them) or an interaction — open.
- mp-duel, every row: 683 reached, 0 diverging, hybrid PASS.
- Proven by the hybrid only: 0x4b7052 (runs as the coroutine, 1 call per scenario), 0x4b6ae5 (only called from it),
  0x4b7f78 (RH_SH_HOST; 2 calls per scenario under replace). Unverified paths (ported from the asm, never taken): the
  DirectPlay arms (0x4b90fb's Send, 0x4b7514's SetSessionDesc, the DLGPROC's provider rows / LBN_*), every load error
  box of 0x4b64d8, 0x4b7a9a's resume / failure / skirmish arms, 0x475e07's kinds the scenarios do not show.
