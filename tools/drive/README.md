# tools/drive — agents drive retail Lords II (`rd`)

An agent authors verified retail traces on its own: it steps the game live, reads state by name, searches for inputs
on in-process savestates, and files the result as a scenario (then corpus → `verify` → the port). The bot
(`tools/bot/`) plays whole games by policy; this covers what nobody played — a dialog's other branch, a random event
made to happen, a battle situation, a screen the bot never opens.

Method = the Kirby playbook (`/opt/src/kirbys-adventure/tas/PLAYBOOK.md`), as openrecet runs it
(`/opt/src/openrecet/tools/drive/`): a trace is a program; a goal is ordered state predicates; the model names state
and designs the search, a local search finds the inputs; done = checked on a fresh replay + looked at.
The agent-facing page is **`DRIVER.md`**; this file is the reference and the campaign recipe.

## What the target provides (measured 2026-11-13)

| level | rhost | figure |
|---|---|---|
| L0 replay | any trace, plain rhost | wine ~4,500 frames/s, Windows ~10,000 (boot + game) |
| L1 step | `rhost --live PORT` (rh_live.c): run N frames with events, read memory, programs per frame, text / hotspots, pictures | a command round trip 0.1 ms local, ~1 ms over a forward |
| L2 snapshot | in-process savestates (rh_snap.c): full page compare, no write tracking | take ~3 ms, restore ~1.5 ms (wine; ~6 MB of state) |
| search | `explore` inside the process (cells, goal, avoid, score, hold); `-j N` sessions on all hosts | per session: wine 6,800 frames/s, Windows 13,700, bunker 16,100; `-j 8` (win 5 + bunker 3) 41,000 |

Why it is right: `rhost.py snap-check` — every blessed scenario through the game coroutine with a savestate at A,
replayed A..B twice after restores: **128/128 runs PASS** (wine, 64 scenarios × 2 windows), and every run's end equals
the blessed corpus (frame, tick, RNG, `state_sem`, every presented frame). `rd verify` after searches with hundreds of
restores: MATCH on win, wine and bunker (same `state_sem` on all three: Lords2 has no host-dependent float state).

Why it is fast: retail is light (a frame ≈ 0.1–0.2 ms), the devices are software (no GPU, no window, no audio), and a
picture is the session's own presented frame (`rd look`: exact, no replay — unlike openrecet's real-device replays).

## Pieces

| file | what |
|---|---|
| `drive.py` (`./rd`) | the stateless CLI: runs, movies, sessions, expressions, search, pictures, text / hotspots, check / verify, cov, filing |
| `DRIVER.md` | the one-page card a driving agent reads |
| `vars.json` | named state (scalars) + records `R(p)` / `C(k)` / `A(i)` with fields, `count(REC, COND)` |
| `ui.json` | named click points (`c@endturn`), with the screens where each is a button |
| `tasks/*.md` | task files: start, ordered predicates, forbids, prose |
| `agent.py` / `agent.sh` | the API-billed driver (Messages API tool loop, budget, log); `agent.sh --check-credit` first |
| `../rhost/rh_live.c` | `--live`: the command server, programs (postfix), the movie, recorders (text, hotspots), `explore` |
| `../rhost/rh_snap.c` | savestates; `rhost.c` the game coroutine (retail on its fixed stack, the host on its own) |

State on disk: `/mnt/ephemeral/openlords2/drive/runs/<run>/` — `movie.jsonl` (a flat rhost trace in the
tests/scenarios format: the run), `meta.json` (start, checkpoints, undo), `vars.json` (the run's own names),
`session.json` (the live process: a cache), `view/*.png`. A session exits after 30 min without a command; the next
command replays the movie.

## Where sessions run (`RD_HOST`)

`auto` (default): the Windows box natively (`ssh $RHOST_WIN`; `export RHOST_WIN=win-wifi` when it is on wifi) when it
answers and admits (≤ `RD_WIN_MAX_SESSIONS` 6 of our sessions, ≥ 1.5 GB free; WMI start, ssh forward alive as long as
the process), else bunker (`tools/bunker/README.md`: wine in the `ol2-bunker-pool` 4 G / 600 % bound, ≤ 8 sessions),
else wine here (≤ `RD_LOCAL_MAX` 6, ≥ 8 GB available). `search -j N` workers spill win → bunker → wine. A session
keeps its host; the exe is content-addressed (`bin\<sha12>\ol2-rhost.exe`) so a rebuild never swaps a running image.

## Screen state for agents

- `rd text R`: the text rows retail drew since the screen opened (call-site recorders on 0x402637 / 0x4025d7), with
  the frame each was drawn — older rows may be covered now.
- `rd hot R`: the hotspot tables retail's input handlers tested in the last frame (0x40e3ee click / 0x40e5e7 hover):
  rect, centre, the action function's IDA name. Search's default actions are clicks on these.
- `rd look R`: the presented frame (PNG). `rd cov R`: retail functions the run reaches that the corpus does not
  (baseline cached at `/mnt/ephemeral/openlords2/drive/corpus_coverage.tsv`; `--refresh-corpus`).
- The recorders run on a private stack and write no retail memory: `rd verify` MATCH with them on.

## Campaign recipe (an orchestrating session)

1. **Pick targets from the gaps**: `docs/findings/coverage-round3.md` "Not covered", `docs/human-traces.md`,
   `tools/bot/README.md` "Not yet", FRONT's unreached list; one behaviour per task.
2. **Check the target is a gap first**: name the retail functions the behaviour runs and grep them in
   `/mnt/ephemeral/openlords2/drive/corpus_coverage.tsv` — split-arm (campaign 1) drove a flow the corpus already
   covered (0 new functions). A task whose functions are covered is a documentation probe, not a coverage one: say so.
3. **Write the task** (`tasks/NAME.md`): `start:` a scenario (prefix replays are cheap), ordered `steps:` as predicates,
   `forbid:` lines, prose with the findings / IDA functions. If the state is not named yet, say which function writes it.
4. **Lint** predicates on runs that do the thing (`rd lint TASK --on RUN`).
5. **Hand each task to a driver** with `DRIVER.md` + the task file:
   - **a subagent of the Claude Code session** (the default; the parent session's model): "read tools/drive/DRIVER.md
     and tools/drive/tasks/NAME.md; your run is NAME; report per the card";
   - **`tools/drive/agent.sh tasks/NAME.md --run NAME --budget N`** when there is API credit: `agent.sh --check-credit`
     (exit 4 = none) before a campaign; it checks again at start.
6. **Accept:** `rd check` PASS, `rd verify` MATCH, `rd cov` (new retail functions), a picture of the moment looked at.
   Then `rd file R NAME` → `rhost.py corpus --bless --only '^NAME$'` (wine + win) → `rhost.py verify --only '^NAME$'`
   (the port's rows on it) → commit with the evidence line.
7. **Keep what was learned:** names → `vars.json`, points → `ui.json`, game facts → `DRIVER.md`, tool gaps → fix the tool.

## Known limits

- Search is wall-time bounded (not reproducible): apply what you got.
- Text rows persist per screen id; a panel drawn over the map leaves the map's rows (age shown).
- Host code at the frame boundary leaves stack residue that some retail code reads (field-orders' state differs
  between compilers: `build.sh` refuses anything but the pinned mingw). Live sessions run the boundary on the same
  game stack as plain replays, so they match plain replays of the same build.
