# tools/bot — autoplay: an LLM plays Lords of the Realm II through rhost

Goal: exercise many game situations against **retail**, quickly, and keep each game as a deterministic
scenario for the corpus. The bot plays the strategy layer and, since v3, takes the field: a battle is
auto-resolved or fought on retail's battlefield (`battle.py`). A game's trace replays with no LLM and no network
(`rhost.py run bot-s3-t16`), bit-identically on Windows and wine.

```sh
python3 tools/bot/play.py --turns 16 --seed 3                 # one game (wine, local) → tests/scenarios/bot-s3-t16/
python3 tools/bot/play.py --turns 16 --seeds 2-7 -j 6         # six games in parallel (~1 min)
python3 tools/bot/play.py --turns 40 --seed 9 --target win    # on the Windows box
python3 tools/bot/play.py --turns 30 --seed 72 --start custom:England --persona diplomat   # a custom game, 5 lords
python3 tools/bot/play.py --turns 44 --seed 204 --start custom:England --persona steward   # v3: a long game (events)
python3 tools/bot/play.py --turns 8 --seed 103 --start bot3-s103-t28 --persona warlord    # play on from a game's end
python3 tools/bot/l2state.py endturn-2 --compact | tail -1    # the state the bot reads, as JSON
python3 tools/rhost/rhost.py corpus --bless --only bot-s3     # adopt a game into the corpus (+ --target wine)
python3 tools/rhost/rhost.py coverage                         # what the corpus exercises in retail
```
(Wrap in `tools/dev`; on the agent host one game at a time unless tiny — `-j` runs games in parallel, wine processes.)
Each game writes `tests/scenarios/bot3[tag]-s<seed>-t<turns>[-k]/` (`--prefix`; v1 / v2 games are `bot-*` / `bot2*`; tag `cen` = custom
England, `x` = played on from another scenario) (never overwritten: Jev's probabilities vary a
little between calls, so the seed fixes the sampling stream but not the game — the trace is the artifact): `trace.jsonl` (all input; the scenario),
`decisions.jsonl` (per turn: state summary, plan, what the UI actions achieved, Jev's raw answers),
`turns.tsv` (crowns, counties, population, happiness, armies per turn).

## How it works

| piece | role |
|---|---|
| `l2state.py` | reads realms / counties / armies / merchants / prices from retail memory (`docs/findings/game-state-map.md`) |
| `llm.py` | OpenRouter: **Jev** decisions (`~typesafe/jev-latest`, `POST /api/alpha/decisions`) — typed questions in, probabilities out; `chat()` fallback |
| `policy.py` | a persona per game (warlord / opportunist / castellan / diplomat / steward) in Jev's brief; per county: tax, rations, industry split, draft (scores), castle, hire the mercenary band offered (noul); per army: an order (choice: claim a neutral town, attack a lord's ungarrisoned county, besiege a garrisoned castle, garrison / leave our castle, combine armies, hold — attacks under the persona's floor of the defence not offered; a persona's appetite multiplies Jev's war probabilities; a march under way kept unasked at 0.85); with an ally: gift / compliment / plead for help / ask to attack (the pleas only once retail would grant them); `ask`: popups with a choice, confirm dialogs, siege engines, **the battle prompt** (auto-resolve or a field tactic). Rations never exceed what the herd's dairy feeds (`sustainable_ration`); the steward's economy is narrowed further. **Samples** from Jev's probabilities (temperature, seeded) so games diverge |
| `ui.py` | settle the map: report scrolls, event popups (0x53e910: notices dismissed on their seal, yes/no kinds answered), confirm dialogs 0x1e, siege preparations 0x1d, the battle prompt 0x12 (asked: auto-resolve, or take the field → `battle.fight`), result scroll; a custom game's defeat ends on the title menu (0x1f) → `GameEnded`; End turn, panels |
| `battle.py` | the battlefield (0x28–0x2b): pause, recentre on our units (its minimap), box-select them, recentre on the nearest enemy, run, RMB it (attack); "Mop up" when the fight drifts; tactics `attack` / `mopup` / `retreat` (once losing) / `autocalc` (after a skirmish; also the timeout); RMB through the end scroll |
| `actions.py` | raise an army (+ hire mercenaries), march (town / castle / another army, confirms by intent), leave a castle, set rations, trade, diplomacy (gift / compliment / insult to any lord; plead for help / ask to attack via an ally, on the plea map), Options > Advanced > "Fight humans only?", a custom game from the title; timing per retail's mouse model |
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

The battlefield (v3, live 2026-10-24; offsets in `docs/findings/game-state-map.md`):
- "take the field" = the LEFT gauntlet (345,128) of the 0x12 scroll; it lands on 0x29 **paused** (the deploy screen
  0x28 is skipped; an RMB starts it when it shows);
- the control panel (480..639 × 448..479, 32-px buttons): hourglass (pause toggle), retreat (confirm "Retreat from
  field?" / "Surrender castle?"), the siege defender's action, **"Mop up enemy troops"** (every unit charges), autocalc
  (confirm "Autocalc battle?"); the battle confirms use the strategic dialog's thumbs (240,218) / (287,218);
- the field's minimap (480..639 × 24..183, 2 px a hex) **recentres the camera only when nothing is selected or the
  battle is paused** — with a selection, running, an LMB there is a march order to that hex;
- selection = an LMB drag of more than 24 px over the field (screen 0x2a while held); it works while paused. Orders
  (RMB) need the battle running; an enemy under the cursor (0x5533b8) makes it an attack;
- no scrolling is needed: minimap recentres + hex ↔ pixel = (col − camera) × 32 + (0, 24);
- a battle ends when a side has no men left (0x53f028 / 0x553c58) → 0x2b "The Battle is won / lost" (an RMB ends it
  now, else ~2500 frames) → aftermath → 0x13; retreat and autocalc end through FUN_0043be65 instead. A field battle
  takes 1,300–9,000 frames (rhost runs them in a few seconds).

## Cost and speed (2026-10-23; v3 2026-10-24)

Jev: ~0.2–0.4 s and ~$0.0001 per decision request (output tokens are free); a 16-turn game ≈ 100
requests ≈ $0.015. The key is the project's own `openlords2-bot` ($3/month cap; `infra keys` in the work
repo). Games run at the speed of rhost: 8 turns ≈ 9 s wall under wine, several in parallel. Custom games cost
more per request (14 counties of state): a 44-turn steward game ≈ 126 requests ≈ $0.03.

## Measured value

Retail coverage (`rhost.py coverage`, 2,611 IDA functions / 844 KB):

| corpus | functions | code bytes |
|---|---:|---:|
| 4 hand-made scenarios | 661 | 37.1% |
| + 7 bot v1 games (≈ 1 min of play, $0.08) | 759 | 41.8% |
| + boot-movies + 8 bot v2 games (trade, gifts; 20 turns each, $0.04) | 817 | 44.6% |
| corpus of 33 (2026-10-23, after later v1 games) | 865 | 46.4% |
| + 3 bot2 games (war, sieges, alliance; 28–30 turns; $0.19 of Jev for the 21 games played to find them) | 914 | 48.8% |
| + `econ-tour` (authored) | 928 | 49.1% |
| + 11 bot3 games (the battlefield, long custom games; $0.33 of Jev for the 17 games played) | 1209 | 63.5% |

The bot2 games, and what they first reach (`rhost.py verify --only`, `coverage`):

| scenario | what happens | newly reached |
|---|---|---|
| `bot2-s64-t28` | opportunist, quick battles; mercenaries, garrison / leave / combine; the Knight takes Taup, the bot besieges it (2 rams → assault) | `combat_begin_siege`, siege setup / board (0x4a7e0a / 0x4a7eb5), the siege screen 0x421f14, the assault 0x4a8aab, `combat_capital_defender` (the Knight invades the bot's county), `combat_load_muster`, `combat_transfer_army`, confirm dialog render / open |
| `bot2-s61-t28` | opportunist; the garrison-capacity popup (type 17, "split your army?") answered | 0x4376bb (its handler), the AI's intercept orders |
| `bot2cen-s72-t30` | custom game (England, five lords), diplomat; the Countess offers an alliance (popup 180, type 11), Jev accepts; the bot starves by turn 12 and the AI lords play on | `engine_diplomacy_form_alliance`, 0x436872 (the answer), the custom-setup screens + config, events (witch, wolves …) |

The bot3 games (`rhost.py verify --target wine --only bot3`: 191/218 hooks reached; every game adds functions no other
corpus scenario reaches):

| scenario | what happens | newly reached |
|---|---|---|
| `bot3-s103-t36` | warlord, Play Now, 28 turns + 8 played on: field battles (attack to the end, retreat), the siege of Taup (2 catapults) whose assault is fought on the field | the battlefield: 240 functions / 110 KB (unit AI, movement, combat, render, the control panel), `battle_end` 0x43be65, `battle_end_tick` 0x477dfc, `battle_pick_result_row` 0x478419, `battle_stage_siege_side` 0x4aa6ca |
| `bot3-s106-t32` | castellan, Play Now: claims Taup from the field (autocalc), loses it to the Knight, attacks it again; disbands its garrison | `ai_disband_into_realm` 0x438681 (+ the troop panel's disband 0x43733a / 0x43744f), ev_89 |
| `bot3-s104-t32` | warlord, Play Now: a siege of Periwinkle, auto-resolved | 0x4192b1 / 0x419354 |
| `bot3cen-s202…s211` (8) | steward, custom England, 26–44 turns: county 13 kept past 1268 (s207 also claims Caernarvon), the alliance offer accepted, gifts / compliments / ask to attack (refused), the Knight's invasions fought on the field; a defeat ends on the title menu | 20 of the 24 realm-event handlers (+ claim_empty / vacate_owned; with s106's ev_89 and bot2's ev_134 the corpus reaches 22 — ev_137 / ev_13d never rolled), the ally's reply to a plea 0x4a38db, `engine_diplomacy_clear_treaty` (the alliance ends) |

## Not yet

The economy is still crude: no labor grid or field sowing, so a county's food is its herd's dairy (5 a cow) and a
growing county caps out; the steward survives 26–44 seasons, then plague or a mercenary bill ends it. Missing: the
labor grid, send supplies, splitting armies, arming troops, lifting a siege from the map; the custom setup's own
options (difficulty, nobles, crowns, "Fight?") — only the map is chosen. On the field the bot knows one manoeuvre
(select the densest group, attack the nearest enemy, mop up): no formations, no ranged / melee split, no castle-wall
tactics; the lord is never captured (FUN_0048d9ce / halve_garrison unreached). The **pay-for-help popup (type 10)**
has not come: an ally answers a plea with it only when its troop strength (realm +0x14) is at least 750 / 800 / 900 /
1000 by personality and its attitude toward us is 10 or more (FUN_004a3551 / FUN_004a38db) — the allies offered so far
(the Countess, the Bishop) stayed under their bar, and every alliance ended within ~12 seasons. The bot's own attack on
a lord's ungarrisoned county: ordered in `bot3-s106-t32` (Taup, three seasons running), but the Knight's army
intercepted the march; `combat_capital_defender` is reached only by the AI's invasions (six bot3 games). Alliance offers cannot happen on Play Now (two lords: FUN_004a1241 skips the leader on
both sides); use a custom game. Realm events roll only for odd-numbered counties (`docs/findings/game-state-map.md`):
Play Now's human county 2 never gets one.
