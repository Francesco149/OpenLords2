# rl mpnet2 — the SIGS teardown, leaving MP, the SierraNW callbacks, the DirectPlay dialogs (2026-11-14)

Partition `mpnet2` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`; MP under rhost:
`multiplayer-rhost.md`; the transport before it: `rl-mpnet.md`): 23 functions MP round 2 (mp-trio / skirmish / siege /
five / dp / leave) newly reaches. `src/rl_mpnet2.c`, rows `tools/rhost/rh_hooks_rl_mpnet2.c`, globals
`src/rl_mpnet2_globals.{h,c}` (generated). All 23 ported; the pending file is empty.

## Routes

| VA | what | row | reached by |
|---|---|---|---|
| 0x4b64a3 | the session's destructor body: Close while connected, DAT_004e5a08 = 0 | TC | mp-skirmish-p2 |
| 0x4b6a15 | Close: +4 / +0 cleared, the monitor's stop flag + the wait 0x4b6ef7, CloseDLL + FreeLibrary | TC | mp-skirmish-p2 |
| 0x4b6ab6 | GameClose (+68), +0 / +12 cleared | TC | mp-skirmish |
| 0x4b6ef7 | wait 8 s for the monitor thread, log, GetExitCodeThread, CloseHandle | TC (pops 4) | mp-skirmish-p2 |
| 0x4b92a0 | the scalar deleting destructor (0x4b64a3, operator delete 0x4c29d0) | TC (pops 4) | mp-skirmish-p2 |
| 0x4b743b | leave MP: DirectPlay DestroyPlayer + 0x4b7180(1) / SIGS 0x42df30, title page 1, 0x4b6ab6 | V | mp-skirmish |
| 0x4b756b | 0x43f01d; 1 | R | mp-trio, mp-war |
| 0x4b79e3 | TellMeSpecialChatReceived's callback: "%s: %s" → save 149's slot 0, event 246 (popup 20) | V (custom) | mp-leave-p1 |
| 0x4b7a7c | TellMePlayerLeftGame's callback: DAT_004e5a90[player] = 1 | R | mp-leave-p1 |
| 0x4b8243 | DirectPlayCreate → QueryInterface(IID_IDirectPlay2A, &DAT_004e2a2c) → Release | V | mp-dp |
| 0x4b82bf | DirectPlayEnumerateA's callback (stdcall 5): a provider row, its GUID as item data | R std, HOST | mp-dp |
| 0x4b8335 | DLGPROC 0x6c: Host 1004 / Join 1002 (or IDOK) / Cancel 2 | R std | mp-dp |
| 0x4b8439 | DLGPROC 0x74: the session name ("Lords2 Session1"), EM_GETLINE into 0x4e5a18 | R std | mp-dp-p1 |
| 0x4b85b7 | DLGPROC 0x82: EnumSessions (callback 0x4b8b87), the 3 s timer, Open join + its result log | R std, HOST | mp-dp-p2 |
| 0x4b8b87 | EnumSessions' callback: the session GUID → 0x4e5a80, its name row, DAT_004e2a34 = 1 | R std, HOST | mp-dp-p2 |
| 0x49792e | the MP lobby's game start / MP save pump: 5 globals, 0x43fed2, the text field, has-data flags, npc schedule | R | mp-skirmish, trio, war |
| 0x479e0c | the first live battle unit of an owner: its WORD +0x178 | R | mp-skirmish, trio, war |
| 0x43695d | accept a peer's diplomacy offer: dismiss, partner, mode 9 / 10, MP message 73 | V | mp-war-p1 |
| 0x43338e | the DirectPlay "Full game / Skirmish" choice: page 11 / 8, the text field, MP message 84 | V | mp-dp-p1 |
| 0x42d40b | the skirmish .skr check: checksum vs the peer's → DAT_0055cd34 | R | mp-skirmish |
| 0x40ec2c | one message pump step (else 0x4b8c3a), the quit byte → WM_CLOSE | R, HOST | mp-trio-p1 |
| 0x420630 | the MP skirmish setup page (as 0x42051c + the backdrop and the opponent's ready colour) | V, MEDIA | mp-skirmish |
| 0x476488 | popup panel 20: "From Beyond" + the lobby chat line | V, MEDIA | mp-leave-p1 |

## Rows

- **Thiscall**: rh_hooks_rl_mpnet.c's HOOK_TC copied (its statics are per file): ECX saved at the stub, the row's
  `this` is the port's first argument, the retail side through a per-row thunk loading ECX. None runs on the monitor
  coroutine's stack (the bypass is kept). The waits inside Close (0x4b6ef7 → WaitForSingleObject on the monitor handle →
  rh_net runs the coroutine to its end) shadow fine: rh_net's meta (the coroutine's registers + stack) is in the rollback.
- **Stdcall** (`HOOK_R_STD(NAME, POP, …)`: RH_ENTRY with `ret $POP`): the three DLGPROCs and the two enumeration
  callbacks. **RH_SH_HOST** where a call adds a row to rh_win.c's fake list box (g_lb / g_lbn — host state the shadow
  does not roll back; a second LB_ADDSTRING lands on the next row): 0x4b82bf, 0x4b8b87, 0x4b85b7 (its EnumSessions calls
  0x4b8b87). 0x4b8335 / 0x4b8439 only touch idempotent host stores (EndDialog's result, WM_SETTEXT, SetFocus): shadowed.
- **0x4b79e3** (cdecl varargs): SierraNW passes the sender's name BY VALUE after the text pointer (rh_net: a 32-byte
  char-array struct); retail prints `"%s: %s"` with `&arg_4`. The row's body keeps e0 and passes e0 + 8 (the name on
  retail's stack), not the adapter's argument copy.
- **0x40ec2c**: RH_SH_HOST (PeekMessage / GetMessage / DispatchMessage move rhost's message queue).
- HOOK_V where EAX is the caller's or a callee's no caller reads: 0x4b743b, 0x4b79e3, 0x4b8243, 0x43695d, 0x43338e,
  0x420630, 0x476488.

## Quirks (retail ground truth)

1. 0x43338e tests DAT_005c9a74 == 2 for title page 10 and then jumps past that branch unconditionally: the Skirmish
   choice (with ≤ 2 players) always goes to page 11.
2. 0x4b85b7's timer id is a local zeroed at every entry: WM_TIMER / IDCANCEL / the end all KillTimer(dlg, 0) and log
   its result + 1; only WM_INITDIALOG's own call sees SetTimer's id.
3. 0x4b85b7 WM_COMMAND: HIWORD 0 with any id but IDCANCEL (not only IDOK) opens the selected session; a failed Open
   closes / releases DirectPlay and EndDialog(0), then still EndDialog(1) (the last result wins: 1).
4. 0x4b8439 stores the name's length in the byte 0x4e5a19 — the high byte of EM_GETLINE's capacity WORD at 0x4e5a18
   (the low byte is whatever 0x4e5a18 held): the capacity is length × 256 + that byte.
5. 0x4b6ef7 (retail bug, noted in multiplayer-rhost.md): TerminateThread gets the HANDLE*'s value, not the handle —
   only on a nonzero exit code (never under rhost: the coroutine exits 0).
6. 0x420630 reads no argument (its callers push the tab); it re-reads DAT_005533e0[DAT_0056d5cc]
   for the second test; [2] is DAT_005533e8 (engine_army_state's g_army_state_index mirror).
7. 0x4b79e3 does nothing on the battlefield (DAT_0057c8fc == 2); the text is cut at 200 bytes into realm 0's MP
   message slot (save 149), whatever the sender.

## Seams

Switched to the ports (callers that are verified rows elsewhere): rl_mpnet.c Close / GameClose / the delete / the
provider connection (was the tc0 / tc1 adapters, engine_retail_call); rl_io.c 0x4b8c3a's disconnect path (Close +
delete, was nw_call — removed) and 0x4b756b; rl_quit.c 0x4b7180 (it called the thiscall Close cdecl without ECX and the
delete without its session — a bug on a path no scenario takes); rl_ui2.c mp_leave_session / mp_lobby_load; rl_ctl.c
(×2) / rl_render.c leave-MP; rl_turn.c popup panel 20; rl_ui4.c 0x420630. Left retail-by-VA: rl_ui2.c's msg_pump
(0x40ec2c: a message pump keeps engine_retail_call's mirror sync, as 0x4b7a9a), rl_ui4.c's save_49792e (called from
the naked frame guard), rl_mpmsg.c's 0x4b743b (another agent's file this round), the DLGPROC pointers (DialogBoxParamA
takes retail's address).

## The seven MP-reached functions owned elsewhere

All seven are ported; none takes a row:
- 0x40e890 / 0x40e8bb (ui4): host level — they run a frame (rhost's frame boundary); retail's only caller is WinMain,
  whose RH_SH_HOST row (rl_main) runs the port's loop, so the hybrid runs both ports on every scenario. A row would run
  a second frame in a shadow.
- 0x42e5c0 / 0x42ec91 / 0x42f1df (ui4): create DirectDraw / the surfaces / the palette. Every retail caller
  (0x4b284e, 0x4b0ab5, the wndproc 0x4b29be) is a ported RH_SH_HOST row whose port calls these ports directly — in the
  hybrid no call reaches the retail entries, so an RH_SH_HOST row would count 0 calls. Proof stays rl-ui4.md's
  replace-only corpus check + the hybrid through the callers.
- 0x465622 / 0x465761 (terr): not safe — rl-terr.md: the executor 0x465d28 reads uninitialized waypoint locals and the
  paged shadow's fault frames land there; a row FAILs the hybrid with rl_46521f until the two share one residue model.

## Evidence

- `verify --target wine -j 3 --only '^mp-' --hooks '<the 23 rows>'`: **23/23 reached, 0 divergences, hybrid PASS** (25
  scenarios). Shadowed calls: 0x4b64a3 1, 0x4b6a15 1, 0x4b6ab6 4, 0x4b6ef7 1, 0x4b92a0 1, 0x4b743b 4, 0x4b756b 5,
  0x4b79e3 1, 0x4b7a7c 1, 0x4b8243 2, 0x4b8335 4, 0x4b8439 2, 0x49792e 7, 0x479e0c 11, 0x43695d 1, 0x43338e 1, 0x42d40b
  14, 0x420630 75, 0x476488 127. RH_SH_HOST (retail in the shadow passes, proven by the hybrid only): 0x4b82bf 2,
  0x4b85b7 3, 0x4b8b87 2, 0x40ec2c 1 (sampled).
- With the seams, the 23 rows + their callers' rows (0x433155, 0x4335f0, 0x497a34, 0x4b8c3a, 0x4b7a9a, 0x4b7f78, 0x42e060,
  0x47309e, 0x43d649, 0x4b7585, 0x41fdd6, 0x4b7180) over the 25 MP scenarios: **34/35 reached, 0 divergences, hybrid
  PASS** (0x4b8c3a 696889, 0x47309e 33996, 0x4b7585 28, 0x4b7a9a 26, 0x4b7180 1 …; 0x42e060 not reached).
- Unverified paths (ported from the asm, never taken): 0x4b6ef7's timeout / abandoned / failed-wait arms and the forced
  termination; 0x4b743b's DirectPlay arm; 0x4b85b7's WM_TIMER, IDCANCEL and failed-Open arms; 0x4b8439's IDCANCEL;
  0x4b8335's IDCANCEL; 0x40ec2c's message arm and WM_QUIT; 0x43338e's Skirmish arm (mp-dp-p1 picks Full game); 0x476488's
  kinds other than the one mp-leave shows.
