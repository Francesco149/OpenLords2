# Driver card: a verified retail trace of Lords of the Realm II with `rd`

For a coding agent with a shell (a Claude Code session or subagent). You turn a task — ordered state predicates, or
"reach / exercise X" — into an input movie that a fresh retail process replays to the same state. Everything runs
on our machines and is free; your turns are the cost, so work in big strides: one `play` or `search` per sub-goal,
not one per click.

`rd` = `/opt/src/OpenLords2/rd` (run it from the repo root). Retail runs as a live process you step frame by frame:
state is numbers, `rd text` and pictures. A picture is free here (`rd look`: the session's own frame, exact) — look
whenever a screen is new to you; Read the PNG.

## The loop

1. **Read the task and what it points to** (findings docs, IDA names). Say in state terms what each step means.
2. **Create your run:** `rd new <run> --from <start>` (the task's `start:`). Never write into another run.
3. **Per sub-goal: act, then read state.** `rd play R "SPEC" --until EXPR` stops on the frame EXPR turns true. For
   anything that needs trying (which button, which county, a random roll), **search instead of steering**:
   `rd search R --goal EXPR --apply` (below).
4. **When something does not happen, find out why before repeating it:** `rd status`, `rd text`, `rd hot`, `rd look`,
   `rd eval`, `rd ram R mark` → act → `rd ram R diff`. Never run the same failing thing twice.
5. **Checkpoint before anything you might redo:** `rd ck R save NAME`; `rd ck R load NAME` returns in a millisecond.
   A load is a step back in time: the movie is cut there, later checkpoints are dropped. `rd undo R` reverts the last
   state-changing command (8 deep).
6. **Finish:** `rd check R TASK.md` → PASS (if there is a task file), `rd verify R` → MATCH, `rd cov R` and `rd cov R --blocks` (the retail
   functions / basic blocks your run reaches that no corpus scenario does), and you have LOOKED at the moment the trace is about. Report:
   run name, frames, each step's frame, state you named, game facts learned, `rd cov` numbers, what the tool lacked.

One command at a time per run (a run is locked while a command works). Give shell calls a timeout above a search's
`--time-limit` (default 60 s; 300 s is safe).

## Commands

```
rd new R --from fresh | SCENARIO[@F] | RUN[:CK|@F]   [--force]     a scenario = tests/scenarios/<name> (its prefix replays: ~10k frames/s)
rd play R "SPEC" [--until EXPR] [--avoid EXPR] [--watch EXPR,EXPR] [--every N] [--rows N]
rd status R      rd text R [--all]      rd hot R      rd look R [--at F,F]      rd strip R --from F --to F [--every N]
rd eval R EXPR [EXPR..] [--at CHECKPOINT | --at-frame F] [--raw]
rd scan R --from F [--to T] --watch "EXPR,EXPR" [--every N] [--changes] [--where EXPR]
rd ram R list | read VA [N] | dwords VA [N] | str VA | define NAME --addr HEX --type s32 | --expr "..." [--status] [--event]
rd ram R mark ; (act) ; rd ram R diff [--lo VA --hi VA] [--save NAME] [--minus NAME]
rd ck R save NAME | load NAME | list        rd undo R        rd rewind R --to F
rd search R --goal EXPR [--avoid EXPR] [--cell "(a, b)"] [--actions SPEC ..] [--time-limit S] [--apply] [-j N]
            [--score EXPR [--apply-best]] [--max-len N]
rd rec R A|C|R [SLOT]   (a record's fields: the pieces in use, a county, a realm)
rd cell R COL,ROW ..    (where a map cell is on screen now)
rd seasons R N [--until EXPR] [--watch EXPR,..] [--battle auto|field]   (End turn N times, the bot's settling: scrolls,
                        popups, confirms (no), sieges (no engines), battles auto-resolved / fought; ~0.1 s a season)
rd covered VA|NAME ..   (is a retail function reached by the corpus, by which scenarios — before you start)
rd check R TASK.md      rd verify R      rd cov R [--blocks]      rd lint TASK.md --on RUN      rd movie R      rd stop R      rd ls
```

**Input spec** (640×480 game coordinates; tokens separated by spaces; `*N` repeats a token; `( … )xN` a group):

| token | does |
|---|---|
| `.` / `.*N` | wait 1 / N frames (a frame = 17 virtual ms) |
| `cX,Y` / `rX,Y` | left / right click: move there, 2 frames, button down, 2 frames, up, 2 frames |
| `bX,Y:N` / `BX,Y:N` | hold the left / right button N frames (auto-repeat arrows) |
| `mX,Y` / `hX,Y` | move the cursor (+2 frames) / hover: move and rest 20 frames (retail acts on mouse idle) |
| `dX,Y-X2,Y2` | left-button drag (box selection, sliders) |
| `kESC` `kENTER` `kSPACE` `kF1` `kA` `k5` | a key (down, char, up) |
| `tTEXT` | type text (`_` = space) |
| `eM:W:L` | a raw window message |

`X,Y` may be numbers, a named point `@endturn` (`tools/drive/ui.json`), a map cell `@cell(13,16)` or a piece `@A(14)`
(resolved on the live viewport; on screen only), or `{EXPR},{EXPR}` read from state at that
boundary (`c{C(3).capital_col * 32},{...}` — rarely needed). Example: `"c@endturn .*400"`, `"r300,240 .*20"`.

**Retail's input model** (the bot learned it the hard way, `tools/bot/README.md`): an in-game LMB on the map **fires
300 ms (~18 frames) after release** and only if no UI element took the press → space map clicks ≥ 25 frames; a march
order needs the cursor to **rest on the destination** first (`h`), then the click; RMB backs out of panels and
dismisses report scrolls (`r300,240`); the first use of a feature pops a hint scroll (screen 0x27: RMB); an event
scroll on the map (`modal != 0`) is dismissed by a LEFT click on it (RMB does not) and eats the next map click.
A season runs in real time: sim ticks advance while you think, AI moves happen at fixed ticks, and the RNG (`rng_b`)
steps once a frame — how long you wait before End turn changes every roll of that season (probe ev-13d: a realm event
made to fire by modelling the LFSR; `rng7`, `phase`).

**Expressions** (goals, `--until`, `--watch`, `eval`, task steps): Python syntax over named state (`rd ram R list`),
`frame`, raw reads `u8(va) s8 u16 s16 u32 s32 f32` (nestable), `+ - * / // % & | ^ << >> ~`, comparisons, `and or
not`, `x in (1, 2)`, `a if c else b`, `min max abs int sqrt hypot`. **Records**: `R(p).crowns` (realm / lord p=1..5),
`C(k).owner` (county k), `A(i).men` (map piece i: armies, rebels, merchants, carts); the index is any expression
(`R(me).crowns`, `C(sel_county).population`). `count(C, owner == me)` counts slots whose fields satisfy COND (field names
bare, `I` = the slot); `count(A, kind == 1 and owner == me, 1, 40)` over slots 1..40 only (a long count is split for
eval / watch; as a goal it must fit one program: narrow the range).

## Search: how to think

`rd search` is the Kirby playbook's score-free exploration, inside the game process on savestates. It keeps one
savestate per **cell**, expands rarely-visited cells with random macro-actions until the **goal** holds, then spends
`--refine` seconds shortening the path. ~7,000 (wine) – 16,000 (bunker) frames/s per session; `-j N` adds sessions on
the other hosts.

- **Goal:** the step's predicate, or a stepping stone (`screen != 0`, `turn == 5`).
- **Cell:** what counts as a different situation. Default `(screen, state, sel_county, frame // 300)`. **Put the
  mechanic's state in it** — the screen id, a dialog's choice, the selected county / piece, a slider value, the season.
- **Avoid:** everything that must survive (`my_counties >= 1`, `crowns > 1000`, `screen != 28`).
- **Actions:** each one a short spec that ENDS WITH A WAIT (clicks need frames to act). Default: a click on every
  hotspot retail's input handlers tested on this screen (`rd hot`) + the ui.json points for the screen, each followed
  by 25 frames, and a plain 25-frame wait. Give what the sub-goal needs: `--actions "c558,469 .*200" "r300,240 .*20"
  ".*100"`. Waits of different lengths before an End turn change the RNG draws a season uses — that is how a search
  makes a random event happen.
- `found: false` lists the newest cells = how far it got. One cell = your actions change nothing the cell sees.
- `--score EXPR` (higher = closer) gives direction; `--apply-best` keeps the best cell's path when the goal is not met.
- Nothing is kept unless `--apply` / `--apply-best` (or `--save FILE`).
- Wall-time bounded ⇒ not reproducible: apply what you got.

## Finding state

- `rd ram R mark`, do the thing, `rd ram R diff`: every dword that changed. Drop noise: mark, wait (`rd play R ".*30"`),
  `diff --save noise`; then mark, act, `diff --minus noise`. Retail .data is 0x4d2000..0x5cee60; the record blocks are
  realms 0x57bf00 + p·0x160, counties 0x53f9b0 + k·0x300, pieces 0x52f0b0 + i·0x1a4 (`docs/findings/game-state-map.md`).
- Name what you find: `rd ram R define NAME --addr HEX --type s32 --event` (your run's vars.json). Report names worth
  sharing; do not edit `tools/drive/vars.json` yourself.
- The game's own names: IDA export `/mnt/ephemeral/openlords2/decompiled-ida/all_ida.c` (`dword_57C934` = VA 0x57c934),
  `functions_ida.tsv` (callers / callees); findings `docs/findings/INDEX.md`; the port's sources `src/` (comments name
  retail VAs). `rd hot` shows each hotspot's action function by name.
- Trust state for progress, pictures for "does this look like the thing". Validate a predicate where you know its
  answer before relying on it.

## Lords II facts (this workbench)

- **Screens** (`screen`): 0 the map (strategic), 2 workforce, 4 terrain info, 8 merchant, 9 court, 0xa armory, 0xb
  diplomacy, 0xc trade, 0xd arm troops, 0xf labor, 0x10 army move, 0x11 split army, 0x12 / 0x13 the battle prompt
  (left gauntlet (345,128) = take the field, right (383,128) = auto-resolve), 0x14 population, 0x15 tax, 0x16
  happiness, 0x17 raise army (+ mercenaries), 0x18 send supplies, 0x19 rations, 0x1a diplomacy action, 0x1b castle,
  0x1c victory / defeat, 0x1d siege preparations, 0x1e confirm (thumbs (240,218) yes / (287,218) no), 0x1f the title
  menus, 0x20 rankings, 0x22 a movie, 0x23 / 0x24 turn processing, 0x27 a hint / report scroll (RMB), 0x28..0x2b the
  battlefield (0x29 running, 0x2b "the battle is won / lost").
- `state` (engine): 2 title, 3 in a game. `turn` 1 = the first season; `season` 1 Spring .. 4 Winter; `year`; `me` = the
  human's realm; `sel_county`; `crowns`, `my_counties`, `my_armies`; `modal` ≠ 0 = an event scroll is up;
  `awaiting_input` = the map, no scroll, the human mid-turn.
- **Starts:** `newgame-playnow` (Play Now, Winter 1268, the map, Raw Sienna = county 2, 5000 crowns, 2 lords),
  `custom-strong` (France, five lords, every county > 1000 souls), `bot3cen-s207-t44` (custom England, 44 turns played:
  a long game, two counties), `siege-castles`, `econ-tour` (the economy screens), `bot3-s103-t36` (battles fought on the
  field). `tests/scenarios/*/trace.jsonl` heads say what each is. Alliance offers need ≥ 3 lords (a custom game).
- **End turn** = `c@endturn` (558,469); a season's processing takes ~90–400 frames (`screen == 35` meanwhile); reports
  and event scrolls then wait for RMB (`r300,240`).
- Realm events roll only for **odd-numbered counties** after 1268 (`docs/findings/game-state-map.md`).
- The battlefield (`tools/bot/README.md` §battlefield): it opens paused; control panel 480..639 × 448..479; selection =
  a drag > 24 px; RMB = order (an enemy under the cursor = attack); minimap 480..639 × 24..183 recentres only with
  nothing selected or paused.
- **Armies** (probe split-arm): an army needs ≥ 50 men ("Army too small"); raise = map icon `c@raise_army` → slider
  `c@raise_more` (+4–5 men a click, `drafted`) → `c@raise_continue` twice; an army's info panel (screen 4) = RMB on an
  army that is NOT selected (RMB on a selected one deselects): move / disband / split (`@army_split`, refused once the
  army moved); Army Division 0x11: per class row y = 128 + 32·i, arrows (268,y) / (298,y), `c@split_yes`. Weapons are
  assigned on the armoury wall (screen 0xa: `@armory_bows`, `@armory_swords` → overlay 0x0d `@assign_plus` …). Play
  Now starts with 25 swords and 25 bows. `rd rec R A` lists the pieces (armies) in use, `rd rec R C 2` a county.
- `rd hot` misses screens whose input uses code hit tests (Army Division, the armoury wall, overlays): look + click.
- **Economy** (probe castle-labor): the castle screen `c@castle_open` (designs `@castle_palisade` … `@castle_royal`,
  `c@castle_yes`); Play Now's Raw Sienna starts with a palisade (`C(2).castle == 1`); `castle_building` 1 = building
  (new or upgrade, `castle` = the level being built, `castle_prev` the old), 2 = repairing siege damage. Clicking a
  castle under construction on the map toggles "Building off". The Town Center (workforce, screen 2) opens on a click on
  the county town; workers move by a box drag over figures (`dX,Y-X2,Y2`, screen 5 → 6) then a click on a task area
  (~26 frames); a click on a task area with nothing selected opens its detail scroll (screen 15, RMB). Fields: a click
  on a fallow field opens terrain info with `@field_fallow / _wheat / _cattle`. The merchant: click its wagon → screen
  8 → a good → trade 0xc (`@trade_up` … `@trade_yes`; hold `b153,226:120` to auto-repeat). County record fields
  `C(k).castle_workers`, `wood_workers`, `idle_workers`, `castle_wood_needed`, `castle_labor_left` …
- `--until` is checked every frame (a press it interrupts is released at once) and, when the spec does not end in a
  wait of its own, waits up to `--pad` (default 3000) frames more. `rd scan R --from F --to T --watch X --changes` finds when something changed in your own movie.
- **Closing panels** (probe hotkeys): every panel has a close seal at `c{seal_x + 12},{seal_y + 12}`; RMB on the map
  at (300,240) opens terrain info — "back" on the map is not neutral. Keys: F8 reloads the previous turn's autosave (old
  turn); F2 cycles a display mode; ESC / File > Quit / New Game ask a confirm (0x1e: answer thumbs-down); never F1 (WinHelp)
  or Alt-1 (writes into the game dir). The menu bar: `@menu_file` `@menu_options` `@menu_help`.
- **The map** (probe terrain-info): RMB on a map cell (no army on it) opens terrain info (screen 4) for that cell, recentres
  the camera on it and changes `sel_county`; RMB on the panel closes it. `rd cell R COL,ROW` says where a cell is on
  screen now (read only); off screen: recentre via the minimap (a county) or edge-scroll (cursor at x=0). In custom-strong
  event scrolls keep arriving up to ~1,100 frames into a season — wait for quiet before map clicks.
- **Pieces** (probe piece-kinds): RMB on a piece opens its info panel (screen 4, like terrain info: test
  `A(sel_piece).kind`, and `sel_piece` is not reset by a terrain panel); a hover just before the RMB makes it fail —
  `r@A(14) .*40`. An envoy scroll eats the next RMB. An enemy entering a county town raises the county's militia (an army
  of ours, "aaaaaa"): losing that battle loses the county. A march onto a wood / mountain cell is silently refused.
  Rebels: a county under 25 happiness climbs `C(k).unrest_band` 0..4; at 4 rebels spawn (50 % tax: ~5 seasons). The
  castle panel's "View these troops" arrow (355,425) works on enemy castles too. Battle result screen 0x13: `c@result_continue`.
- **AI lords** (probe siege-knights): realm +0x84 + 0x10·q = attitude toward realm q (−30..30), +0x87 + 0x10·q the
  vendetta stage (battles at −30 advance it), +0xEB the vendetta target, +0x4b the current target county; an AI picks
  a garrisoned castle last (+40 in 0x4a03f2) unless its vendetta is you. Knights need armour (realm +0x154, only from
  a smithy). A keep holds 200 men (more → a split prompt, the split-off half garrisons); at 0 people a county stays
  yours while its castle is garrisoned. AI raiders take a county's herd in 1–2 seasons.
- `rd cov R --blocks --fn NAME` lists a function's blocks nobody reaches yet — what to aim at next.
- Loads of game files take one frame here; never wait by wall time, wait for state (`--until`).
- A run that was poked (`rd ram R w`) is POKED and cannot be filed.

## What not to do

- Do not read other runs' movies or `tests/scenarios/*/trace.jsonl` events for the inputs of your task: the trace must
  be your own. Reading the scenario HEADERS (what a start is), findings docs and the decompile is the point.
- Do not kill processes; `rd stop R` ends your session. Do not touch other projects' files on the shared machines.
- Do not commit; report instead.
