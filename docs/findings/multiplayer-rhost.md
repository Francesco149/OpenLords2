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

## Leaving and lobby chat (2026-11-14, `mp-leave`)

Two delivery kinds besides messages, stamped by receive poll the same way: `{"n": N, "p": P, "left": 1}` (the
broker sends 'L' to the others when a side quits) and `{"n": N, "p": P, "chat": "HEX"}` (injected into a live side:
rh_bot.c `netchat`, mpbot `pK chat P TEXT`). At the stamped poll rhost calls what the real SierraNW calls from its
own thread: TellMePlayerLeftGame's callback 0x4b7a7c(player) (DAT_004e5a90[player] = 1 → the pump 0x4b8c3a →
0x43e6a2(2, player + 1): the departed human's realm leaves the map) / TellMeSpecialChatReceived's 0x4b79e3(text,
name) — a type-20 popup "From Beyond … Player2: text". **0x4b79e3 prints `"%s: %s"` with `&arg_4`**: SierraNW passes
the sender's name BY VALUE inline on the stack after the text pointer (a char-array struct), not a pointer. Under
DirectPlay a departure arrives as DPSYS_DESTROYPLAYERORGROUP (5) from DPID 0. mpbot `pK leave` quits one side's
process while the others play on.

## The last two MP divergences (2026-11-14, evening) — both harness

- **Shadow-only, mp-skirmish-p2 f32583** (rl_42ff10 / 4ba9c8 / 432b05 / 4b7250 on 16 dwords: tick stamps 0x4e59d4 /
  0x4e59e0 / 0x4e5a7c, the clock string 0x4e5b48): the post-Back rejoin resumes the session, waits for the monitor's
  GameStart (0x4b63d0, ~1 s), then Close 0x4b6a15 does `CloseHandle(hObject)`. In the shadow, retail's run closed the
  REAL event; the pre-state restore gave the port side the same fake → dead real handle → WaitForSingleObject =
  WAIT_FAILED at once (rhost.log: the port side printed "NewGame ACK" with no MonitorThread-GameEnd / GameStart).
  Fix (rh_handles.c): inside a shadow a non-file handle's CloseHandle is deferred; the real close happens at the first
  meta restore whose table no longer holds it.
- **Hybrid / standalone, mp-leave-p1 f3352** (0x55cd6c 3 → 4, 0x5656f0 13 → 1; first differing frame found with
  `--hash-every`, then `--watch-write`): Player2 leaves → 0x43e6a2(2) → resign 0x49cf20 → demolish 0x4ac3c6 →
  plan_realm 0x4485a5 → the army-economy evaluator, whose scratch pair is a FILE-STATIC mirror (engine_army_economy.c,
  synced by rh_port_sync_in / _out). Later in the same port chain the frame driver 0x4b99c0 is entered BY ADDRESS
  (rhost's frame boundary) → its row's ctx_port → rh_port_sync_in reloaded the mirrors from retail memory and dropped
  the evaluator's result. Fix (rh_hooks_ai.c): a row entered from live port code (no retail since the last sync_in)
  stores the mirrors first (sync_out, then sync_in) — the general form of rl_crt.c's alloc-hook workaround.

**Resolved 2026-11-14 (late): mp-trio-p3** — quirk #240. The note that stood here watched 0x58fffd98, 0x47309e's own
local; 0x475e07's [ebp-8] is 0x58fffd60 (entry esp 0x58fffd6c, `--trap 475e07`), engine_tick's body esp − 92. Its last
writer each map frame is the scene's `call engine_map_render_tile` from row_phase0 / phase1 (return address 0x405ac5),
then 0x475e07's kind, then 0x47309e's tail push of the box y. The port models it (`rl_475e07_res_in`). Standalone
`--only '^mp-'`: **24/25 = pure retail**. **mp-trio-p1** (fb only; state_sem equal on all 56975 frames,
`--hash-every 1`): the first differing presented frame (`--extra=--fb-hash`) is f53108, the MP lobby's first drawn
frame — the local seat's name "aaaaaa" red in retail, blue in the standalone. The roster port reads its seats from
the row adapter's snapshot (`seats_load` in TR()), taken BEFORE the port's prep seam runs 0x49caaa, which reassigns the
seats' name colours (+8); retail reads the records after it. Fix: the prep seam reloads the seats. **Standalone 105/105
= pure retail** (whole corpus, wine).

## MP verify (2026-11-15)

`rhost.py verify --only '^mp-' --target wine --wine-timeout 18000` (mp-war's shadow passes need > 1 h a run: the new
`--wine-timeout`): **1541/1829 rows reached, 0 divergences, hybrid PASS on all 25 sides**; 288 never called. Its one
failed run — mp-war-p2, shadow pass 3, end state_sem ≠ corpus with tick / screen / RNG equal throughout — was harness:
per-frame hashes put the first split at f19245 (the tooltip's backdrop buffer 0x4e6980..0x4e8780, every byte +1, copied
from the back buffer by 0x4b3f0a; presented frames equal), and a parallel bisect of the pass's 1280 shadow rows (4 rounds,
8 groups a round, end state at f19246) left one: `rl_4b1310`, which greys the 640×480 back buffer IN PLACE (a lobby /
the county's fall). Its row had no RH_SH_MEDIA, so the shadow did not roll the surface pixels back between the retail
and the port side: greyed twice, and `.data` (all the plain shadow compares) agreed. Fixed: the row is RH_SH_MEDIA
(pixels compared and rolled back); mp-war-p2 with it shadowed ends = corpus (4 calls, 0 divergences). Audit: no other
rl_ port writes the back buffer DAT_004ea1a8 directly.


## Ids 38 / 58 / 93 / 94 (2026-11-15: `mp-labor`, `mp-skrpick`)

- `mp-labor`: the Town Center's workforce grid double-clicked (0x439df0 → 38: both sides run 0x439edb) and three task
  sites toggled on the map (cells with flag 0x80 in the county's own land: 0x43ce1a → 0x43d309, then 58). The 0x80
  cells of the visible map: `mapview.visible(side)` + the cell bytes (+1 & 0x80, +0 the type, +7 the county); hover does
  not set DAT_005440b0 — only the click does.
- `mp-skrpick` (skirmish type): the setup's user-map box lists `*.skr`; a pristine game has one (USER.SKR), already the
  chosen name, so 0x434174's pick is a no-op. A second one comes from the trace's start files:
  `{"meta": {"saves": ["game:USER.SKR=user2.skr"]}}` (rhost.py `stage_save`: the user's own game file copied under a
  new name into the run dir at run time — no game data in git; rd keeps the entry as is). Picking user2.skr → 93 →
  the peer loads it (0x42d4aa) and answers 94. While authoring over mpbot: copy the file into each side's
  `p1.s.rundir` before the setup page lists the files, and add the meta line to the saved traces.

## Desync, lag and the resync (2026-11-15: `mp-desync`)

Tools (live only — a replay takes the stamps from the trace, so neither needs a setting there):
- **`pK lag MS`** (mpbot; rh_bot.c `netlag`, rh_net.c `rh_net_lag`): side K's arrivals become visible only once its
  virtual clock is MS past the clock the sender sent them at, in order (a slow link).
- **`pK forge ID OFF XOR [N] [crc]`** (rh_bot.c `netforge`, `rh_net_forge`): the next N distinct (by seq) messages of id
  ID arriving at side K get byte OFF (0 id, 1 size, 2 checksum, 3 seq | copy flags, payload from 4) XORed; the checksum
  follows unless `crc` (then 0x43eac1's "Bad packet CRC" branch). The trace stamps the forged bytes.
- **`feed SCEN UNTIL`** (mpbot): every side's input events from an existing MP scenario re-posted at their frames, the
  deliveries live through the broker — mp-duel re-fed to its battle prompt f15930 reproduced it exactly; branch off there.

**Lag cannot make 90 or 91 happen.** Retail paces each side's ticks by its view of the peers' (DAT_0053e8d0, from the
peers' messages 10 / 12): on the strategic map a side ahead is slowed by tiers (0x4bb978's caller), on the battlefield it
stops dead ~40 ticks past its view (p1 lagged 400 s, p2 paused: p1 held at 520 with its view of p2 at 480 for 20k
frames). A late action (due = lead 50 + the highest known tick, 0x446e21) needs the receiver 50+ ticks past the sender's
view; the battlefield's turn-byte check (tick % 130 == 45, 0x440dd2 → 2) needs the peer's message 12 of that period
still in flight 45 ticks on — under lag the receiver just waits. Lag 20–60 s on mp-start / mp-duel states: 0 late
actions, DAT_00512f88 never above 0. 90 stays unreached (a real link that drops and re-orders would be needed).

**`mp-desync`** (2 sides, 25054 frames): mp-duel fed to f15930, p2 takes the field at Perth, p1 unpauses; p1 `forge 12 10
1 1` — p2's next message 12 carries hash sum [0] ^ 1 (a desynced peer). p1's 0x440666 (f16957): 0x440dd2 → 0 → the
snapshot restore 0x43f5c5 → message 16; the restore makes the states really differ, so the resync ping-pongs: 16 both
ways (4 / 5 times), p1's turn bytes differ three checks running → 91 to p2, DAT_00553214 reaches 3 on p2 (0x479a71 for
both sides). Each restore leaves the other side paused (DAT_0053f238 = −1): p1 then p2 unpause, and from f22654 the
battle runs in step with no further resync. Windows ≡ wine, det 0 dwords.

## An AI ally's price (2026-11-15: `mp-price`)

The setup page's options are the host's (p1 clicks; the client's page follows): County status (577,303) → the list
weak / medium / strong (577,335 = strong). With strong every AI realm's average county population is > 1000 at the
start, over every personality's ask-to-attack bar. Diplomacy (622,447), the Baron's portrait (90,190): compliment →
attitude +15 at the turn's end (reply 174); "Offer an alliance" (415,268 for a non-ally) → 179, the alliance mutual
(+0x81); "Ask to attack" → 186 / 187 / 188 by `rng` vs 2 × attitude; 188 is the type-10 popup — tools/bot's UI answers
it no by default (`uis[0].ask` overridden to yes). Message 70 → action 40: rl-mpmsg3.md §Message 70.
