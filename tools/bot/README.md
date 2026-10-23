# tools/bot — autoplay: an LLM plays Lords of the Realm II through rhost

Goal: exercise many game situations against **retail**, quickly, and keep each game as a deterministic
scenario for the corpus. The bot plays the strategy layer; battles auto-resolve. A game's trace replays
with no LLM and no network (`rhost.py run bot-s3-t16`), bit-identically on Windows and wine.

```sh
python3 tools/bot/play.py --turns 16 --seed 3                 # one game (wine, local) → tests/scenarios/bot-s3-t16/
python3 tools/bot/play.py --turns 16 --seeds 2-7 -j 6         # six games in parallel (~1 min)
python3 tools/bot/play.py --turns 40 --seed 9 --target win    # on the Windows box
python3 tools/bot/l2state.py endturn-2 --compact | tail -1    # the state the bot reads, as JSON
python3 tools/rhost/rhost.py corpus --bless --only bot-s3     # adopt a game into the corpus (+ --target wine)
python3 tools/rhost/rhost.py coverage                         # what the corpus exercises in retail
```
Each game writes `tests/scenarios/bot-s<seed>-t<turns>[-k]/` (never overwritten: Jev's probabilities vary a
little between calls, so the seed fixes the sampling stream but not the game — the trace is the artifact): `trace.jsonl` (all input; the scenario),
`decisions.jsonl` (per turn: state summary, plan, what the UI actions achieved, Jev's raw answers),
`turns.tsv` (crowns, counties, population, happiness, armies per turn).

## How it works

| piece | role |
|---|---|
| `l2state.py` | reads realms / counties / armies / merchants / prices from retail memory (`docs/findings/game-state-map.md`) |
| `llm.py` | OpenRouter: **Jev** decisions (`~typesafe/jev-latest`, `POST /api/alpha/decisions`) — typed questions in, probabilities out; `chat()` fallback |
| `policy.py` | per county: tax (score), rations (score), industry split (score), draft (score); per army: march target (choice). **Samples** from Jev's probabilities (temperature, seeded) so games diverge |
| `ui.py` | settle the map (report scrolls, battle prompt → decline to take the field = auto-resolve, result scroll), End turn, panels |
| `actions.py` | raise an army, select + march an army on a county's town, set rations, trade at the merchant, send a gift; timing per retail's mouse model |
| `mapview.py` | cell ↔ screen from retail's own viewport geometry and tile grid (hover-confirmed), edge-scroll to a cell |
| `play.py` | the turn loop + output |

Retail's input model, as the bot learned it (all through rhost's virtual queue, so all in the trace):
- an in-game LMB click **fires 300 ms after release** and only if no UI element consumed the press
  (`engine_input.c`, `DAT_004e65f0`) → map clicks are spaced ≥ 25 frames;
- a move order needs the cursor to **rest on the destination** first (retail computes the path preview on
  mouse idle; a destination click without one is ignored);
- the minimap recentres on a **county**, not a point; arbitrary cells come into view by parking the cursor
  on the 1-px window border (retail's edge scroll, also in army-move mode);
- the first use of a feature pops a hint scroll (0x27; RMB dismisses).

## Cost and speed (2026-10-23)

Jev: ~0.2–0.4 s and ~$0.0001 per decision request (output tokens are free); a 16-turn game ≈ 100
requests ≈ $0.015. The key is the project's own `openlords2-bot` ($3/month cap; `infra keys` in the work
repo). Games run at the speed of rhost: 8 turns ≈ 9 s wall under wine, several in parallel.

## Measured value

Retail coverage (`rhost.py coverage`, 2,611 IDA functions / 844 KB):

| corpus | functions | code bytes |
|---|---:|---:|
| 4 hand-made scenarios | 661 | 37.1% |
| + 7 bot v1 games (≈ 1 min of play, $0.08) | 759 | 41.8% |
| + boot-movies + 8 bot v2 games (trade, gifts; 20 turns each, $0.04) | 817 | 44.6% |

## Not yet

The bot plays badly on purpose-free sampling (over-drafts, starves, often loses its county; it then plays the
defeat out — the victory/defeat screen 0x1c — and continues in the scenario retail restarts). Missing: castle
building, send supplies, the labor grid and fields, alliances/insults, sieges; interactive battles; custom-game
setups (maps, opponents, difficulty); games with `"movies": true` (the movie paths: jail, victory, defeat).
`select_army` sometimes fails for an army stacked with another or inside a town.
