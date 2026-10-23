# Hybrid batch 4 — the economy / UI-action side under rhost (2026-10-24)

Scope: the screens' models — the merchant trade grid (`src/engine_trade.c`), the diplomacy-action (gift / message /
ally) screen (`src/engine_diplomacy_action.c`), Send Supplies and the county-action tab strip that opens it
(`src/engine_sendsupplies.c`), the merchant rug (`src/engine_merchant.c`) — plus the economy executors they call in
`src/engine_army_ai.c` / `src/engine_diplomacy.c` (keep build, caravan, disband, the diplomacy queue). Table:
`tools/rhost/rh_hooks_economy.c` (22 rows). New scenario: `tests/scenarios/econ-tour`.

## Result

- **22 hooks added; 218 in all.** `rhost.py verify --target wine -j 8 --only .` over the 43-scenario corpus (the 36
  of batch 3, `econ-tour`, and the 6 bot3 games the autoplay agent had added by then, not yet blessed): 201/218
  reached, **0 divergences**, 17 never called. **Hybrid pass** (all 218 replaced): the 37 blessed scenarios
  bit-exact (state + every frame); the 6 bot3 games report `NO expected.json` only (every pass), so the evidence run
  needs them blessed first.
- Batch-4 rows: 21 of 22 reached (all but `ai_disband_into_realm`); the bot games reach 12, `econ-tour` the other 9
  (the Send Supplies screen, buy all / sell all / cancel, the caravan executor).

| hook | VA | depth | calls | scenarios |
|---|---|---|---|---|
| merchant_zone_click | 0x4357a6 | 1 | 11981 | 28/43 |
| trade_open | 0x43530e | 2 | 167 | 28/43 |
| trade_compute | 0x428daf | 3 | 167 | 28/43 |
| trade_ale_delta | 0x435673 | 2 | 3970 | 28/43 |
| trade_arrow_up | 0x435339 (entry) | 1 | 3390 | 27/43 |
| trade_arrow_down | 0x43543d (entry) | 1 | 575 | 17/43 |
| trade_buy_all | 0x4355db (entry) | 1 | 3 | 1/43 (econ-tour) |
| trade_sell_all | 0x435541 (entry) | 1 | 2 | 1/43 (econ-tour) |
| trade_seal_confirm | 0x435286 (entry) | 1 | 165 | 28/43 |
| trade_seal_cancel | 0x4352f2 (entry) | 1 | 1 | 1/43 (econ-tour) |
| diplo_arrow | 0x436372 (entry) | 1 | 1695 | 29/43 |
| diplo_seal | 0x436408 (entry) | 1 | 174 | 29/43 |
| diplo_queue_action | 0x4a2621 | 2 | 174 | 29/43 |
| ai_build_keep | 0x436d02 | 1 | 20 | 14/43 |
| county_action | 0x43ae30 (entry) | 1 | 413 | 37/43 |
| ss_arrow_up | 0x43b27a (entry) | 1 | 4 | 1/43 (econ-tour) |
| ss_arrow_down | 0x43b1ca (entry) | 1 | 1 | 1/43 (econ-tour) |
| ss_toggle_all | 0x43b32a (entry) | 1 | 2 | 1/43 (econ-tour) |
| ss_target_select | 0x43b412 | 1 | 295 | 1/43 (econ-tour) |
| ss_commit | 0x43b145 | 1 | 1 | 1/43 (econ-tour) |
| ai_dispatch_caravan | 0x4292af | 2 | 1 | 1/43 (econ-tour) |
| ai_disband_into_realm | 0x438681 | - | 0 | 0/43 |

Entry-patched rows (no E8 call site — the router tables DAT_004dd838 / DAT_004dd9d0 / DAT_004dda30 / DAT_004dd538 /
DAT_004dc7e8 / DAT_004dc688; rhost 4358af1): the trade arrows / seals, the gift arrow, the diplomacy seal, the
county-action tabs, the Send Supplies arrows / all-toggle. A `--shadow-poison 0x554170` run reported on them
(305/305), so the entry patch is shadowed like a call-site hook.

## Restructuring: models split from renders

The screens' logic sat in the same files as their renders, whose gfx / io / localization closure rhost cannot link.
Each screen now has a model file — `engine_trade_model.c`, `engine_diplomacy_action_model.c`,
`engine_sendsupplies_model.c` — that rhost links (build.sh PORT_OBJS, with `text_input`); the renders stay in the
original files. The retail globals they drive moved to `src/engine_economy_globals.c` (not linked; aliased in the
table): the caravan order DAT_005678c0, the nine trade figures, the diplomacy mode DAT_005651cc / latch
DAT_0052afa4, the Send Supplies target DAT_00553c78, the castle tab's tier DAT_0056d898 (#236).

Retail-layout wrappers (the port's engine.c router keeps its own glue; these are the retail entry points, used by
rhost and ready for the standalone to adopt): `engine_trade_compute_current` (FUN_00428daf from the save globals),
`engine_trade_open` / `engine_merchant_zone_click` (FUN_0043530e / FUN_004357a6), `engine_trade_seal_confirm` /
`_cancel` (FUN_00435286 / FUN_004352f2), `engine_diplomacy_action_seal` (FUN_00436408, every mode — the port had
mode 0 only: now the lords2.inf message copy, the ally-target notices 0xf0..0xf4, the vassal refusal 0xdb),
`engine_county_action_open` (FUN_0043ae30 — the whole tab strip, incl. the castle tab FUN_00436a88 and the
diplomacy tab FUN_0043611b), `engine_sendsupplies_target_select(ox, oy, …)` (retail's origin arguments).

## What the shadow / the reading found

- No divergence on identical state. The bot games and `econ-tour` exercise every row but `ai_disband_into_realm`.
- **#236** (read while moving the globals; none reached): the caravan order and the trade figures were file-statics
  (the #234 pattern — a hooked FUN_004292af would have shipped an empty private order); FUN_00428daf leaves goods
  3 / 5 negating the previous holdings; buy-all / sell-all totals have no sign branch; the trade arrows' s068 cue;
  FUN_004a2621 debits then credits (a self-gift nets 0).

## Seams → retail

`FUN_00427990` (the s068 / s033 cues), `FUN_00416308` (the trade grid's open render, from `engine_trade_open`),
`FUN_004b3714` (the county's s016 army cue on the armory tab), and the armory tab's entry — `FUN_00435ebc`
(conscripts at the slider %) + `FUN_004aa90a` (muster pool) + DAT_005679d0 / DAT_0055446c = 0 — because
`engine_armory.c` has no model split yet (its ports have non-retail signatures). Bridges across the link cut:
`engine_menu_button_accel` (DAT_00591554), `config_player_prefs` (DAT_0053f1e0, lords2.inf). The seal's editor
clear goes through a port `text_input_state_t` loaded from / stored to retail's scattered editor globals.

## Never called — what reaches them

| row | reached only via |
|---|---|
| ai_disband_into_realm 0x438681 | "disband army" (confirm kind 6 → FUN_0043744f), MP FUN_00447d9d, or the AI's garrison order FUN_004a6270 with a full keep — human-traces B3 |

`ai_build_keep` at depth 1 is the human's build-castle path (FUN_00436b59, the castle screen's button); the AI's
keep builder FUN_0049edc7 reaches it inside its own shadow.  `trade_compute` sits at depth 3 under the rug click
and the open.  The counts include the (unblessed) bot3 games' shadow passes.

## Not hooked

- **Labor screen** (`engine_labor.c`): render-only apart from the open hit test FUN_0043a123 (+ FUN_004398f5 /
  FUN_0045183a / FUN_004517ca); the corpus never opens the workforce grid (screen 2) or labor (0x0f) — coverage 0 —
  and the screen's model (the worker drags FUN_004399b0 / FUN_00439df0 → FUN_00439b52 / FUN_00439edb /
  FUN_00439f6a, the row fill FUN_0045161e …) is unported. Human trace B2, or a bot that opens the grid.
- **Treasury** (`engine_treasury.c`): render-only; its seal FUN_004351c4 (→ Rankings) is engine.c glue.
- **Send Supplies seal** FUN_0043b04c and its confirm callback FUN_0043b101 (target = source → "quit (no
  destination)?"): engine.c glue; the port's confirm dialog takes no callback VA. `econ-tour` reaches the seal.
- **Gift-screen openers** FUN_00436141 / 0043618b / 004361da / 00436229 / 004362ca / 0043631e (set the mode, run the
  map recomposite) and the castle build button FUN_00436b59: engine.c glue around renders.
- **Armory** (FUN_00435cef slider, FUN_0043582a zone, FUN_00435ebc / FUN_004aa90a): needs the same model split;
  coverage 29 scenarios — the next economy candidate, with the county panels' rations / tax input
  (FUN_0043a379, FUN_00438ceb).

## Tooling

- `rhost.py coverage` (first-hit INT3 over every function, 15 s for the corpus on wine) picked the targets: what the
  corpus reaches, what it never does.
- An authored scenario turns unreached rows into corpus evidence: `econ-tour` branches `bot2-s61-t28` at f7000 and
  drives the screens over rhbot with the bot's `ui` / `mapview` helpers; `det -n 3` 0 dwords, blessed on wine and
  Windows (identical expected).
- Gap: the pre-commit corpus gate runs every scenario directory, including another agent's in-progress games
  without an expected.json yet — it blocks unrelated commits until they are blessed.
- Footgun: `--shadow-poison` parses its VA with base 0 (decimal unless `0x`).
