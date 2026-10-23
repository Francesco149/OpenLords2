# tools/bot — autoplay: an LLM plays Lords of the Realm II through rhost

Goal: exercise many game situations against **retail**, quickly, and keep each game as a deterministic
scenario for the corpus. The bot plays the strategy layer; battles auto-resolve. A game's trace replays
with no LLM and no network (`rhost.py run bot-s3-t16`), bit-identically on Windows and wine.

```sh
python3 tools/bot/play.py --turns 16 --seed 3                 # one game (wine, local) → tests/scenarios/bot-s3-t16/
python3 tools/bot/play.py --turns 16 --seeds 2-7 -j 6         # six games in parallel (~1 min)
python3 tools/bot/play.py --turns 40 --seed 9 --target win    # on the Windows box
python3 tools/bot/play.py --turns 30 --seed 72 --start custom:England --persona diplomat   # a custom game, 5 lords
python3 tools/bot/l2state.py endturn-2 --compact | tail -1    # the state the bot reads, as JSON
python3 tools/rhost/rhost.py corpus --bless --only bot-s3     # adopt a game into the corpus (+ --target wine)
python3 tools/rhost/rhost.py coverage                         # what the corpus exercises in retail
```
(Wrap in `tools/dev`; on maximus one game at a time unless tiny — `-j` runs games in parallel, wine processes.)
Each game writes `tests/scenarios/bot2[tag]-s<seed>-t<turns>[-k]/` (`--prefix`; v1 games are `bot-*`) (never overwritten: Jev's probabilities vary a
little between calls, so the seed fixes the sampling stream but not the game — the trace is the artifact): `trace.jsonl` (all input; the scenario),
`decisions.jsonl` (per turn: state summary, plan, what the UI actions achieved, Jev's raw answers),
`turns.tsv` (crowns, counties, population, happiness, armies per turn).

## How it works

| piece | role |
|---|---|
| `l2state.py` | reads realms / counties / armies / merchants / prices from retail memory (`docs/findings/game-state-map.md`) |
| `llm.py` | OpenRouter: **Jev** decisions (`~typesafe/jev-latest`, `POST /api/alpha/decisions`) — typed questions in, probabilities out; `chat()` fallback |
| `policy.py` | a persona per game (warlord / opportunist / castellan / diplomat) in Jev's brief; per county: tax, rations, industry split, draft (scores), castle, hire the mercenary band offered (noul); per army: an order (choice: claim a neutral town, attack a lord's ungarrisoned county, besiege a garrisoned castle, garrison / leave our castle, combine armies, hold — hopeless attacks not offered; a march under way resampled at 0.35 T); `ask`: popups with a choice, confirm dialogs, siege engines; the game-start quick-battle option. **Samples** from Jev's probabilities (temperature, seeded) so games diverge |
| `ui.py` | settle the map: report scrolls, event popups (0x53e910: notices dismissed on their seal, yes/no kinds answered), confirm dialogs 0x1e, siege preparations 0x1d, battle prompt → decline to take the field = auto-resolve, result scroll; End turn, panels |
| `actions.py` | raise an army (+ hire mercenaries), march (town / castle / another army, confirms by intent), leave a castle, set rations, trade, gift, Options > Advanced > "Fight humans only?", a custom game from the title; timing per retail's mouse model |
| `mapview.py` | cell ↔ screen from retail's own viewport geometry and tile grid (hover-confirmed), edge-scroll to a cell |
| `play.py` | the turn loop + output |

Retail's input model, as the bot learned it (all through rhost's virtual queue, so all in the trace):
- an in-game LMB click **fires 300 ms after release** and only if no UI element consumed the press
  (`engine_input.c`, `DAT_004e65f0`) → map clicks are spaced ≥ 25 frames;
- a move order needs the cursor to **rest on the destination** first (retail computes the path preview on
  mouse idle; a destination click without one is ignored);
- the minimap recentres on a **county**, not a point; arbitrary cells come into view by parking the cursor
  on the 1-px window border (retail's edge scroll, also in army-move mode);
- the first use of a feature pops a hint scroll (0x27; RMB dismisses);
- an event scroll at screen 0 (0x53e910 != 0) eats the next map click, and the click that dismisses it over
  the map makes retail swallow the one after (FUN_0047685d sets DAT_0052afa4) — v1's "cannot select";
- a march ends at the season's end: an order to a county four seasons away is re-issued each season;
- the raise-army slider step varies (~1–4 % of the county a click): read the count back (0x543fd8).

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
| corpus of 33 (2026-10-23, after later v1 games) | 865 | 46.4% |
| + 3 bot2 games (war, sieges, alliance; 28–30 turns; $0.19 of Jev for the 21 games played to find them) | 914 | 48.8% |

The bot2 games, and what they first reach (`rhost.py verify --only`, `coverage`):

| scenario | what happens | newly reached |
|---|---|---|
| `bot2-s64-t28` | opportunist, quick battles; mercenaries, garrison / leave / combine; the Knight takes Taup, the bot besieges it (2 rams → assault) | `combat_begin_siege`, siege setup / board (0x4a7e0a / 0x4a7eb5), the siege screen 0x421f14, the assault 0x4a8aab, `combat_capital_defender` (the Knight invades the bot's county), `combat_load_muster`, `combat_transfer_army`, confirm dialog render / open |
| `bot2-s61-t28` | opportunist; the garrison-capacity popup (type 17, "split your army?") answered | 0x4376bb (its handler), the AI's intercept orders |
| `bot2cen-s72-t30` | custom game (England, five lords), diplomat; the Countess offers an alliance (popup 180, type 11), Jev accepts; the bot starves by turn 12 and the AI lords play on | `engine_diplomacy_form_alliance`, 0x436872 (the answer), the custom-setup screens + config, events (witch, wolves …) |

## Not yet

The bot still plays the economy badly (v2 narrows the choices; Jev still over-builds castles and drafts, and
custom games starve). Missing: the labor grid and fields, send supplies, splitting armies, arming troops,
compliments / insults / asking an ally for help (so the "pay for help" popup, type 10, never comes), lifting a
siege from the map, interactive battles (A1); the custom setup's own options (difficulty, nobles, crowns,
"Fight?") — only the map is chosen. The bot's own attack on a lord's ungarrisoned town has been ordered but
not landed yet (the Knight fortified Taup first): `combat_capital_defender` is reached through the Knight
invading the bot's county. Alliance offers cannot happen on Play Now (two lords: FUN_004a1241 skips the
leader on both sides); use a custom game.
