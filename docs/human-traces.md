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
| A1 | **Interactive battles** — "take the field" (screen 0x12 → battlefield 0x28/0x29/0x2a): drag-select boxes, RMB attack / move orders, formations, battlefield scrolling, retreat | real-time: outcomes depend on order timing and placement; the bot always auto-resolves | open |
| A2 | **Sieges** — siege preparations (0x1d), siege engines, assaulting palisade / motte / stone castles, defending one | multi-step, partly real-time, late-game state | open |
| A3 | **Late-game campaigns** — castles completed and upgraded, several counties, alliances, sieges of stone castles, a victory | the bot rarely survives past ~turn 15 on Play Now; a human gets the game into states the bot can't reach | open |
| A4 | **Responding to the AI's diplomacy** — alliance / tribute proposals and threats that arrive at unpredictable turns, accept / refuse / counter | timing set by the AI; the bot has no handler for incoming proposals yet | open |
| A5 | **Event decisions** — plague, rebellion, bandits, the lord captured (jail.smk → ransom), knights / mercenaries offering service | rare, random-timed, choice dialogs the bot does not recognize (they show as "unknown screen" and get RMB'd) | open |
| A6 | **Natural mouse dynamics** — drags (sliders dragged rather than clicked, labor icons dragged on the labor grid 0x0f / 0x02, minimap drag), held buttons, double-clicks, quick click sequences racing retail's 300 ms delayed-click arbitration | the input path's held-button / motion / dblclk branches; synthesizable in principle but a human's timing covers the cases nobody thought of | open |

### Evidence: what the corpus never reaches in the battle / movement code (rhost verify, batch 2, 2026-10-23)

`docs/findings/hybrid-batch2-combat.md`: of 59 battle / movement hooks, these 9 are never called by the 33-scenario
corpus. The bots always auto-resolve (screen 0x12 → AUTO_RESOLVE, FUN_0043b622), march only on neutral counties and
never besiege.

| function | reached only via | list item |
|---|---|---|
| FUN_004ad8cc halve_garrison; FUN_004ab383's `render` branch (anim deadline, scratch clear) and its captured-loser branch (<50 head → notice 0x120) | the battlefield: aftermath(1) from FUN_0043be65 / FUN_00477dfc (end screen 0x2b); DAT_0056d5c8 = 1 is set only by the battlefield's unit logic (FUN_0048d9ce, battle-hex records) | **A1** |
| FUN_004a7ca2 begin_siege (+ FUN_004a7e0a / FUN_004a7eb5, the siege board) | a stack stepping onto an ENEMY keep cell (executor region-event hi) | A2 |
| FUN_00437535 transfer_army | demolishing a realm that has a pending garrison; the AI's siege order FUN_004a60b9; delayed actions FUN_00447f82 | A2 / A3 |
| FUN_0046d42c capital_defender | marching on a county another player OWNS (the bots only take neutral ones) | A3 (or the bot) |
| FUN_00422c12 load_muster | the 0x13 quick-animation path: the Options battle toggle DAT_0053f284 = 0 (FUN_0043470d / FUN_00447e0b) and not both sides garrisoned | B (an Options toggle) |
| FUN_004a07f2 find_garrison_realm | AI movement planner FUN_004a6270 (an AI keep with spare garrison room) | A3 |
| FUN_0049cf20 resign_player | multiplayer resign / the resign UI | out of scope (MP) |
| FUN_00468ae2 special_tile, FUN_0046873f region_event_lo | a march across a foreign county's untouched special tile / undeveloped resource tile | the bot (route choice) |

## B. Medium — synthesizable with effort, a short human trace is cheaper

| # | interaction | notes | status |
|---|---|---|---|
| B1 | Custom-game setup: map choice, opponents, difficulty, advanced options, title / shield / name entry | long click flows + text entry; one human pass per variant seeds the bot's starts | open |
| B2 | Labor screen in detail (0x0f): per-task workers, field sowing (wheat / cattle / fallow), reclaiming | many small controls; the bot only uses the HUD farming↔industry arrows | open |
| B3 | Army management: split (0x11), merge, arm troops (0x0d), hire mercenaries, garrisons, send supplies (0x18) | each a screen of its own | open |
| B4 | Keyboard: text entry (save names, lord name), hotkeys (F-keys, Alt-letters, Ctrl), Escape semantics | WM_CHAR / WM_SYSKEY paths | open |
| B5 | Focus loss / Alt-Tab → WM_ACTIVATEAPP 0 → retail's paused / surface-restore path | rhost activates once and filters OS activation; needs synthetic trace events, or record-mode passthrough | open |
| B6 | Tooltip hover dwell and edge-scrolling with real mouse movement | timing-driven UI | partial (bot edge-scrolls) |

## C. Synthesized — covered without a human

| interaction | by | status |
|---|---|---|
| title → Play Now → map; entry scrolls; End turn; report scrolls | scenarios newgame-*, endturn-* | synth |
| tax, rations, HUD industry split, drafting, marching on a county's town, auto-resolved battles, defeat → restart | `tools/bot/` (bot-* games) | synth |
| merchant trades, diplomacy gifts, castle construction start | `tools/bot/` | synth |
| File > Save / Load, Options panels, Court, population / happiness reports | menus-tour | synth |
| boot with movies (intro, imptitle, credits) | boot-movies, newgame-movies | synth |

Keep this list current: when a human trace lands, link it in the status column; when the bot learns an
interaction, move it to C.
