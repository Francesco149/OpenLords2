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
