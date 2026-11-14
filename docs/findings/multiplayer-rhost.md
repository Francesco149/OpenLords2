# Multiplayer under rhost — the fake SIGS transport (2026-11-14)

Multiplayer was the largest unreached block (coverage-round3: every MP path keyed on DAT_00553030 / DAT_00553248).
rhost now plays it: `tools/rhost/rh_net.c` (the transport), `tools/rhost/rhnet.py` (the broker + `rhost.py mp-record`).
First game: `mp-start-p1` / `mp-start-p2` (title → Multiple players → name / shield → the MP setup page → Start →
1268 Winter, both humans in the game, identical RNG on both sides). **+85 retail functions / 21.5 KB** over the
corpus baseline from those two scenarios alone; each side replays alone, Windows ≡ wine, det 0 dwords.

## Retail's two transports

Title → "Multiple players" (menu_action_page1 0x432b05, case 2) → 0x4b7250 → 0x4b7585:

- **DirectPlay** (DAT_004e2a2c, IDirectPlay2A from DirectPlayCreate + QueryInterface IID 0x4d0058): modal dialogs
  0x81 (provider list, DialogFunc 0x4b7f78; DirectPlayEnumerateA fills it), 0x6c (host / join, 0x4b8335), 0x82 (session
  list, 0x4b85b7, EnumSessions +52 / Open +96), 0x74 (session name, 0x4b8439). Vtable slots used: +8 Release, +16
  Close, +24 CreatePlayer, +36 DestroyPlayer, +52 EnumSessions, +56 GetCaps, +96 Open, +100 Receive, +104 Send.
- **SIGS** (DAT_004e2a30, a 0x74-byte ASIGSSession, ctor 0x4b61b0): the dialog's "Sierra Internet Gaming System" row
  has item data 0 → EndDialog(1003) → 0x4b7a9a. `SNWValid.dll!SNWValidate` (game, ip_out, &a, &b, dir, exe, hwnd) = 0
  → `SierraNW.dll`, 18 cdecl exports resolved by 0x4b6754 into the session at +44..+112: InitializeDLL(ip, a, b),
  CloseDLL, CheckGameConnectStatus (+64: −1 disconnected, 1 the lobby started the game), GameClose,
  CheckForAnyTCPMessage (+52: **0 = something waits**), PeekForTCPMessage(buf, size, player) → length,
  GetNumberOfPlayers(0), SendTCPMessage(buf, len) (broadcast), SendTCPPointMessage(player, buf, len), RecvTCPMessage(buf,
  len, player), GetPlayerXName(out, player), GetMyPlayerNumber (0 = the session creator → DAT_00553248 = 1),
  GetGameOption("GameType", out) ("Full" / "Skirmish" → DAT_0057cb38), TellMeGameMessReceived, FreeGameMessageBuffer,
  PingTime(2, −1) (≥ 100 → the slow-link flag DAT_00553264), TellMeSpecialChatReceived(1, 0x4b79e3, 0, 0),
  TellMePlayerLeftGame(1, 0x4b7a7c, 0, 0). A player's DPID = its index + 1 (DAT_004e2a28; 0x4b90fb sends point
  messages to dpid − 1).
- The **monitor thread** StartAddress 0x4b7052 (CreateThread in 0x4b64d8): polls CheckGameConnectStatus every 350 ms;
  1 → SetEvent(hObject) ("MonitorThread-GameStart"; the main thread's 0x4b63d0 waits on it, 300 ms timeouts, pumping
  messages: "NewGame ACK"); −1 → session +8 = 1 + SetEvent ("Disconnect" → NAK). 0x4b6a15 (Close) sets +8 and waits
  for the thread (0x4b6ef7, 8 s). Note: 0x4b6ef7 passes `TerminateThread(hThread_ptr, …)` — the HANDLE*'s value, a
  retail bug on a path taken only when the thread's exit code is non-zero.

The receive pump 0x4b8c3a: per call, the 5 "joined" flags DAT_004e5a90 → 0x43e6a2 events; then 0x4b6b71 (CheckForAny →
for i in 0..GetNumberOfPlayers: Peek → Recv) into DAT_00515c50; a message from dpid ≠ ours → 0x43eac1 (the dispatcher).
Sends: 0x43eda0 builds the payload (table 0x4d57f0[id]) into the ring DAT_00512fb0 and calls 0x4b90fb.

## How rhost plays it (rh_net.c)

- `--net-sigs ME/N[,full|skirmish]` (rhost.py passes it from a trace's `{"meta": {"net": …}}`). Off by default: every
  other scenario still gets "no dialog, -1" and no DLL.
- **Dialogs** (rh_win.c): with --net-sigs, DialogBoxParamA runs retail's DLGPROC against a fake dialog whose controls are
  list boxes (LB_ADDSTRING / SETITEMDATA / SETCURSEL / GETCURSEL / GETITEMDATA): WM_INITDIALOG, then IDOK on what it
  selected. DirectPlayEnumerateA enumerates nothing, so dialog 0x81 holds only the SIGS row → 1003.
- **SNWValid / SierraNW**: LoadLibraryA / GetProcAddress / FreeLibrary answer the two names with rhost's functions.
- **The monitor thread is a coroutine** on its own fixed stack (0x5e000000..0x5e040000): it runs when the main thread
  sleeps, waits (WaitForSingleObject on any handle while it lives) or ends a frame, and its own Sleep(350) has elapsed
  in virtual time. Its handle is a constant; Wait / GetExitCodeThread / CloseHandle on it are answered.
- **Deliveries are stamped by receive poll**: the n-th CheckForAnyTCPMessage makes every message stamped ≤ n visible.
  Trace lines `{"n": POLL, "p": FROM_PLAYER, "d": "HEX"}` (rh_win.c's input loader skips them). Sends are digested
  (`net: … sent=N send_hash=…` in rhost.log).
- **Recording** (`--net-peer HOST:PORT`, the broker in rhnet.py): sends go to the broker, arrivals are stamped at the
  next poll and written to the out-trace. Virtual clocks are kept within 200 ms of the slowest live peer (a process
  ahead blocks in real time; virtual time never sees it) — so timeouts in retail's MP code (0x4b7250's 5 s / 10 s)
  behave as on a LAN. A side's trace replays alone because nothing it reads depends on the peer except the stamped
  deliveries.

- **Residue-neutral entries.** rh_net's host code runs on retail's stack, and a retail MP path reads an uninitialized
  local: an rh_net.c edit changed mp-start's state. Every entry from retail into rh_net.c (the SierraNW / SNWValid
  exports, rh_net_tick / _wait / _load / _proc / _free / _create_thread) goes through an asm wrapper that, with
  --net-sigs, zeroes the 16 KB below the caller's esp after the call (without it: a tail jump, a plain call).
- **Fixed export addresses.** Retail stores the GetProcAddress results in its session object (+44..+112), so they are
  state: GetProcAddress hands out `jmp` thunks on a page at 0x5e100000 (one per export, SNWValidate last), never
  rhost's own code addresses. Checked: shifting rhost's code (a dummy function in rhost.c and rh_net.c) leaves every
  MP scenario's state_sem unchanged; so does a 3 KB junk-writing CheckForAnyTCPMessage.
- **Rollback / savestates**: rh_net_meta_save / _restore (counters, the visible queue as deep copies, the monitor
  coroutine's registers + the live part of its stack) join the shadow's host-state rollback (rh_state.c) and rd's
  savestates (rh_snap.c). The broker link is never rolled back.

```
rhost.py mp-record NAME --side P1.jsonl --side P2.jsonl [--frames N] --target wine [--shot-every 500]
    → tests/scenarios/NAME-p1, NAME-p2 (each checked: replay alone ≡ its recording run)
```
A side's input trace = ordinary events + `{"meta": {"net": "ME/N"}}` (side k = player k − 1; player 0 hosts).

**Authoring live: `tools/rhost/mpbot.py`** — N rhbot sessions joined through the broker and always stepped together
(`serve NAME [-n N]` in the background, `do NAME "p2 click 230 170; run 60; both click …; shot TAG"`, `py NAME
"code"` with p1 / p2 as rhbot-Session-like sides so tools/bot's `actions` / `UI` / `l2state` work, `settle_all()` /
`end_turn_all()`, `save NAME SCEN` → SCEN-p1 / -p2, `quit NAME`). A side idle on its controller tells the broker it is
paused ('P'), so the others never wait on it. `mp-play` was authored this way (both raise armies, End turn, a
compliment and a gift between the humans — the receiver gets a type-12 popup, 0x475e07 —, a march into Caernarvon).

## The lobby (inputs that worked, 640×480)

Title "Multiple players" c320,139 → "Choose your title and your shield" (name field, five shields at y≈170: x 141 /
230 / 320 / 410 / 495; a shield another player holds is greyed) → Continue c430,226. The client then waits ("Please
wait while the session creator decides what type of game to play"); the host's Continue only takes once the client's
choice has arrived (f2000 worked, f1750 did not) → the MP setup page (humans + AI nobles on the left, chat box, map
list; the host has Cancel / Start c280,204 / Defaults / Load, the client only Cancel; the option scrolls as in custom
setup) → Start → the game: host Lincolnshire, client the Duchy of Cornwall, the usual opening scrolls.

## Not done yet

- `rd` on an MP scenario: savestates carry rh_net's state now, but drive.py does not pass `--net-sigs` from a movie's
  meta, and its movie truncation drops the `"n"` delivery lines — untested.
- Deeper MP play: end turn in lockstep, diplomacy between humans, battles between humans, chat, a player leaving
  (TellMePlayerLeftGame's callback 0x4b7a7c), the slow-link flag, a 3–5 player game, MP save / load (0x49792e).

## DirectPlay (`"net": "dp:ME/N"`, 2026-11-14)

The other transport, same delivery model (`mp-dp-p1 / -p2`: +9 functions, 3.4 KB — the dialogs 0x4b8335 / 0x4b8439 /
0x4b85b7 and callbacks 0x4b82bf / 0x4b8b87 / 0x4b8243, the DirectPlayCreate thunk, 0x43338e, 0x43e6a2):
- DirectPlayEnumerateA lists one provider ("Internet TCP/IP Connection For DirectPlay", a fixed GUID); rh_win.c's
  dialogs pick it in 0x81 (LBN_SELCHANGE on its row, IDOK → 0x4b8243 → DirectPlayCreate → QueryInterface), then
  0x6c Host (player 0: 1004) / Join (1002), 0x74 the default session name ("Lords2 Session1": an edit control model —
  WM_SETTEXT / EM_LINELENGTH / EM_GETLINE), 0x82 the one session EnumSessions reports (LBN_SELCHANGE + IDOK →
  Open join).
- The IDirectPlay2A object (DAT_004e2a2c), its vtable and 16-byte method thunks live on the fixed export page
  (0x5e100400 / 0x800 / 0x900). Faked: QueryInterface, AddRef, Release, Close, CreatePlayer (DPID = index + 1),
  DestroyPlayer, EnumSessions, GetCaps (not the slow-link triple), Open, Receive, Send, SetSessionDesc (+124, 0x4b7514
  at Start: "no new players"); any other slot is fatal. Every Receive is a receive poll (DPERR_NOMESSAGES 0x887700be
  when none). CreatePlayer / DestroyPlayer send a DirectPlay system message to the other sides through the broker
  ('Y' → 'M' from 0xfe = DPID 0): DPSYS_CREATEPLAYERORGROUP (3) / DESTROYPLAYERORGROUP (5), which 0x4b8c3a hands to
  0x43e6a2.
- **A page SIGS never shows:** after the shield page the DirectPlay host gets "Expansion pack installed, choose: Full
  game (c190,128) / Skirmish (c440,128)" (0x43338e) — under SIGS the lobby's GameOption decided. Then the usual setup
  page and Start.
