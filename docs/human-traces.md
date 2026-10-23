# Human traces — what only real UI play exercises well

Owner request (2026-10-23): keep a list of interactions that are fully exercised only through real UI play,
and flag first the ones that are hard to reproduce reliably without a human trace. Simple cases get
synthesized (autoplay `tools/bot/`, `rhbot.py` scripts); this list is what to ask a human to record.

Record: `python3 tools/rhost/rhost.py record NAME [--from SCEN]` — a normal window on the Windows desktop
(never fullscreen, with sound), real input, realtime; close the window to finish. `--from SCEN` replays SCEN
first and records from where it ends (e.g. `--from newgame-playnow`, `--from bot-s32-t24` for a mid-game
start). The trace lands in `tests/scenarios/NAME/` and replays bit-exactly with no human.

Status: **open** = no trace yet · **partial** = some coverage · **synth** = the bot / a script covers it.

## A. Hard to reproduce without a human — record these first

| # | interaction | why a human | status |
|---|---|---|---|
| A1 | **Interactive battles** — "take the field" (screen 0x12 → battlefield 0x28/0x29/0x2a): drag-select boxes, RMB attack / move orders, formations, battlefield scrolling, retreat | real-time: outcomes depend on order timing and placement; the bot knows one manoeuvre (select the densest group, attack the nearest enemy, Mop up) — formations, archers held back, chokepoints, the lord captured (FUN_0048d9ce, halve_garrison FUN_004ad8cc: unreached) need a human | partial — bot v3 (`tools/bot/battle.py`): `bot3-s103-t36` takes the field, box-selects, attacks, fights to the end, retreats; `bot3-s106-t32` / `bot3cen-*` autocalc from the field, defend; all three end paths reached (input model: `tools/bot/README.md`) |
| A2 | **Sieges** — siege preparations (0x1d), siege engines, assaulting palisade / motte / stone castles, defending one | the assault battlefield (engines, moat filling, walls) is real-time; defending a castle the AI besieges, stone castles | partial — `bot2-s64-t28`: march on a garrisoned castle → "Besiege castle?" → 0x1d (engines chosen by Jev, Proceed / Lift) → auto-resolved assault; `bot3-s103-t36`: the assault on Taup's palisade (2 catapults) fought on the field — the bot's generic attack, no engine handling, no defence of its own castle |
| A3 | **Late-game campaigns** — castles completed and upgraded, several counties, alliances, sieges of stone castles, a victory | the bot still rarely grows past one county (it loses claims to militia, starves custom games); a human gets the game into states the bot can't reach | partial — bot3 steward games (`bot3cen-s202…s211`) keep their county 26–44 seasons, `bot3cen-s207-t44` claims a second; no castle upgrades, no victory |
| A4 | **Responding to the AI's diplomacy** — alliance / tribute proposals and threats that arrive at unpredictable turns, accept / refuse / counter | the "pay for help" request (popup type 10) follows only the player asking an ally for help (diplomacy modes 5/6, not in the bot); alliance offers need ≥ 3 lords (FUN_004a1241 skips the leader on both sides, so never on Play Now) | partial — `bot2cen-s72-t30`: the Countess's alliance offer (popup 180, type 11) accepted by Jev; AI messages (warnings, invasion notices, gift replies) recognized and dismissed in every bot2 game. Bot v3 sends the ally gifts / compliments and pleads (help: FUN_004a3551, attack: FUN_004a38db, reached with its refusal in `bot3cen-s202-t40`); the type-10 answer needs the ally's troop strength (realm +0x14) ≥ 750 / 800 / 900 / 1000 by personality and its attitude ≥ 10 — no ally so far reached its bar before the alliance ended |
| A5 | **Event decisions** — plague, rebellion, bandits, the lord captured (jail.smk → ransom), knights / mercenaries offering service | the lord is captured only on the battlefield (A1); the other events are notices without a choice (popup types 0/3/15: rats, wolves, plague, drought, witch, corruption, unrest ...) | synth — bot2 games: the notices are recognized (0x53e910 = their loc group) and dismissed; mercenary offers (the raise-army panel) and the garrison-capacity split (type 17) are Jev questions. v1's "unknown screen 0x17" was the mercenary panel. Bot v3's long games roll 21 of the 24 realm events (below); the lord captured (jail) still needs A1 |
| A6 | **Natural mouse dynamics** — drags (sliders dragged rather than clicked, labor icons dragged on the labor grid 0x0f / 0x02, minimap drag), held buttons, double-clicks, quick click sequences racing retail's 300 ms delayed-click arbitration | the input path's held-button / motion / dblclk branches; synthesizable in principle but a human's timing covers the cases nobody thought of | open |

### Evidence: what the corpus never reaches in the battle / movement code (rhost verify, batch 2, 2026-10-23; bot2 games added the same day)

`docs/findings/hybrid-batch2-combat.md`: of 59 battle / movement hooks, these 9 were never called by the 33-scenario
corpus (v1 bots auto-resolve every battle, march only on neutral counties, never besiege). The bot2 games
(`rhost.py verify --only bot2`) reach 4 of them: begin_siege, transfer_army, capital_defender, load_muster.

| function | reached only via | list item |
|---|---|---|
| FUN_004ad8cc halve_garrison; FUN_004ab383's `render` branch (anim deadline, scratch clear) and its captured-loser branch (<50 head → notice 0x120) | the battlefield: aftermath(1) from FUN_0043be65 / FUN_00477dfc (end screen 0x2b); DAT_0056d5c8 = 1 is set only by the battlefield's unit logic (FUN_0048d9ce, battle-hex records) | **A1** — the render branch: bot3 field battles; the captured lord / halve_garrison: still unreached |
| FUN_004a7ca2 begin_siege (+ FUN_004a7e0a / FUN_004a7eb5, the siege board) | a stack stepping onto an ENEMY keep cell (executor region-event hi) | **synth**: `bot2-s64-t28` (+ the assault FUN_004a8aab) |
| FUN_00437535 transfer_army | demolishing a realm that has a pending garrison; the AI's siege order FUN_004a60b9; delayed actions FUN_00447f82; the player's "leave castle" (FUN_004374c4) | **synth**: every bot2 game (the bot garrisons and leaves its castle; the AI's garrisons march out) |
| FUN_0046d42c capital_defender | an army contesting a county another player OWNS (no castle, or no garrison) | **synth**: `bot2-s61-t28`, `bot2-s64-t28`, six bot3 games (the AI invades the bot's county; the bot's own attacks on the Knight's Taup were ordered — bot2-s64: Taup fortified first; `bot3-s106-t32`: three seasons' march, intercepted) |
| FUN_00422c12 load_muster | the 0x13 quick-animation path: the Options battle toggle DAT_0053f284 = 0 (FUN_0043470d / FUN_00447e0b) and not both sides garrisoned | **synth**: every bot2 game (Options > Advanced > "Fight humans only?" = yes when Jev chooses quick battles) |
| FUN_004a07f2 find_garrison_realm | AI movement planner FUN_004a6270 (an AI keep with spare garrison room) | A3 |
| FUN_0049cf20 resign_player | multiplayer resign / the resign UI | out of scope (MP) |
| FUN_00468ae2 special_tile, FUN_0046873f region_event_lo | a march across a foreign county's untouched special tile / undeveloped resource tile | the bot (route choice) |

Batch 3 (`docs/findings/hybrid-batch3.md`, 196 hooks, 36 scenarios): of 47 new rows 19 are reached by the corpus
(the siege assault FUN_004a8aab by `bot2-s64-t28`), 24 more only under exploration; never called by the corpus:

| function | reached only via | list item |
|---|---|---|
| FUN_00477dfc battlefield end check (+ FUN_00478419 result row), FUN_0043be65 battlefield end | "take the field" at the 0x12 choice (DAT_0057c8fc = 2): the end check runs every battlefield frame, the end buttons (retreat, autocalc) call FUN_0043be65 | **synth**: bot v3 — `bot3-s103-t36` (+ 9 more bot3 games; end check 139k calls, result row 19, battle_end 9, 0 divergences) |
| FUN_004aa6ca siege-equipment staging | a siege assault with quick battles off (DAT_0053f284 ≠ 0; it runs before the 0x12 choice, fought or not) | **synth**: `bot3-s103-t36` (fought on the field), `bot3-s104-t32` (auto-resolved) |
| 22 of the 24 realm-event handlers FUN_00448e12 … FUN_00449826 (+ FUN_0044c77a / FUN_0044c80a) | the human owning **odd-numbered** counties after 1268 (≈20 % per odd county per season: the roll indexes table 0x4d6108 at 2·rng + county, non-zero only at ≡ 7 mod 8 — Play Now's human county 2 never rolls) | **synth** for 21: bot3 steward games (custom England, county 13: `bot3cen-s202…s211`) + `bot3-s106-t32` (ev_89, Taup); claim_empty / vacate_owned too. Unreached: ev_137 (0x449575), ev_13d (0x449826) — never rolled in 11 games |

Batch 4 (`docs/findings/hybrid-batch4.md`, the economy / UI-action table, 22 rows): the bot games reach the trade
grid, the gift / message / ally seals, the keep build and the county-action tabs; the authored `econ-tour` adds Send
Supplies end to end (arrows, all-toggle, retarget, dispatch → the caravan FUN_004292af, its trip and unload), buy-all
/ sell-all / cancel at the merchant. Never called by the corpus, or not hooked for want of a reaching scenario:

| function | reached only via | list item |
|---|---|---|
| FUN_00438681 disband_into_realm | the player's "disband army" in a county (FUN_0043744f, confirm kind 6), MP FUN_00447d9d, or the AI's garrison order FUN_004a6270 when the keep is full | **synth**: bot v3 disbands its garrison (troop panel's 2nd button → confirm 6): `bot3-s106-t32`, `bot3cen-s210-t44`, `bot3cen-s211-t44` |
| FUN_0043a123 labor open hit test (+ FUN_004398f5 / FUN_0045183a / FUN_004517ca), the labor drags FUN_004399b0 / FUN_00439df0 (unported) — not hooked | the workforce grid (screen 2) → a task cell → labor (0x0f); no corpus game opens either (coverage 0) | **B2** |
| FUN_0043a5a9 county ration set (engine_county_panels, not hooked) | the rations panel's ◄ ► / slider changing the level (FUN_0043a379 runs in 29 scenarios but never changes it) | the bot (C: rations) |

## B. Medium — synthesizable with effort, a short human trace is cheaper

| # | interaction | notes | status |
|---|---|---|---|
| B1 | Custom-game setup: map choice, opponents, difficulty, advanced options, title / shield / name entry | long click flows + text entry; one human pass per variant seeds the bot's starts | open |
| B2 | Labor screen in detail (0x0f): per-task workers, field sowing (wheat / cattle / fallow), reclaiming | many small controls; the bot only uses the HUD farming↔industry arrows | open |
| B3 | Army management: split (0x11), merge, arm troops (0x0d), hire mercenaries, garrisons, send supplies (0x18), disband | each a screen of its own | partial — send supplies: `econ-tour`; merge / mercenaries / garrisons: bot2 games |
| B4 | Keyboard: text entry (save names, lord name), hotkeys (F-keys, Alt-letters, Ctrl), Escape semantics | WM_CHAR / WM_SYSKEY paths | open |
| B5 | Focus loss / Alt-Tab → WM_ACTIVATEAPP 0 → retail's paused / surface-restore path | rhost activates once and filters OS activation; needs synthetic trace events, or record-mode passthrough | open |
| B6 | Tooltip hover dwell and edge-scrolling with real mouse movement | timing-driven UI | partial (bot edge-scrolls) |

## C. Synthesized — covered without a human

| interaction | by | status |
|---|---|---|
| title → Play Now → map; entry scrolls; End turn; report scrolls | scenarios newgame-*, endturn-* | synth |
| tax, rations, HUD industry split, drafting, marching on a county's town, auto-resolved battles, defeat → restart | `tools/bot/` (bot-* games) | synth |
| mercenary hire, garrison / leave a castle, combine armies, besiege (engines, Proceed / Lift), quick battles option, event popups, an AI alliance offer, a custom game (England, five lords) | `tools/bot/` v2 (`bot2-s61-t28`, `bot2-s64-t28`, `bot2cen-s72-t30`) | synth |
| merchant trades, diplomacy gifts, castle construction start | `tools/bot/` | synth |
| take the field: select, attack, Mop up, retreat, autocalc; a siege assault on the field; disband a garrison; compliments, pleas to an ally; a custom game's defeat → title menu | `tools/bot/` v3 (`bot3-s103-t36`, `bot3-s106-t32`, `bot3cen-*`) | synth |
| Send Supplies (arrows, all-toggle, retarget, dispatch, the cart's trip), the trade grid's buy all / sell all / cancel | `econ-tour` (authored over rhbot from `bot2-s61-t28` f7000) | synth |
| File > Save / Load, Options panels, Court, population / happiness reports | menus-tour | synth |
| boot with movies (intro, imptitle, credits) | boot-movies, newgame-movies | synth |

Keep this list current: when a human trace lands, link it in the status column; when the bot learns an
interaction, move it to C.
