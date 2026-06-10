# Per-realm AI economic-management subsystem (`FUN_0049a990`)

The AI "brain" the turn-advance engine runs once per turn for every realm an
AI player owns.  It is the **first call** of the turn driver `FUN_00448440`
(`engine_realm`'s turn sequence; the driver itself is still unported — it gets
assembled once all its passes land).  Ported 2026-06-01 into
[`src/engine_army_ai.c`](../../src/engine_army_ai.c) (the cell-mutation leaves
went to `engine_territory.c`, the appraisal valuation to `engine_army_economy.c`).

All preferred-base (`ImageBase 0x00400000`) addresses.

## Call graph (entry → leaves)

```
FUN_0049a990  run_turn            players 1..5 still in game (strat +0x4 set, +0x5 clear)
  └ FUN_0049dd01  run_player      every realm owned by the player
       develop empty cells toward a population-scaled target (FUN_0044c6c4)
       latch personality dispatch code (+0x1fe) from the tuning table
       └ one of three personality dispatchers:
            FUN_004a4052  balanced      (code 0, strategy tag 0x32)
            FUN_004a42e3  compact       (code 1, tag 0x14)
            FUN_004a440f  expansionist  (code 9, tag 0x28)
              each:
                FUN_0049e39b  reconcile_resources  buy queued / sell surplus vs targets
                  FUN_004a4a3f sell_order / 4b12 buy_order / 4c41 buy_order_scaled
                    FUN_004284ce execute_trade   ← the transaction primitive
                      FUN_00428c42 advance_tier, FUN_00450ccd consume_build_demand,
                      + ported re-plan tail (ranking / territory / economy)
                FUN_0045158b seed_workforce_ratios
                FUN_004a4782 tax_sweep            sweep +0x15f to max provision net +0x15d
                  FUN_0044e6a3 appraisal_value (economy), FUN_0044e5bb stock_a_value
                FUN_004a4694 balance_high_cells
                FUN_004697cd demolish_band / 46988d convert_or_demolish_band (territory)
       allocate workforce (FUN_0044f6e7)
```

22 functions, ~9.3 KB, fully self-contained (it does **not** reach the larger
diplomacy/AI cluster behind the driver's other head calls `FUN_0049b2a3` /
`FUN_0044aa41`).

## Data structures

| memory | what |
|--------|------|
| `g_per_army_768` (0x53f9b0, 0x300 stride) | per-realm record: census buckets +0x1ff/0x200/0x201, stocks +0x224/0x250, strategy tag +0x8, dispatch code +0x1fe, tax/band/net +0x15d..0x15f, trade gate +0x1a4, race idx +0x1a5, treasury +0x1f4 |
| `g_player_strategic_352` (0x57bf00, 0x160 stride) | per-player: active +0x4, eliminated +0x5, personality +0x7, buy requests +0x70/0x74/0x7c, cash +0x118, commodity pools +0x120/0x128/0x130, cashflow ledgers +0x104..0x110, trade lock +0x158 |
| cell roster + grid (`DAT_0053ea00` / `DAT_00522f90`) | the same feature-cell roster + descriptor grid `engine_territory` owns; the develop/demolish leaves stamp cell TYPE bytes through it |
| `s_slot_005532a0` (save entry 82) | 16-entry × 0x10 B commodity price table (base prices) |
| `g_per_territory_420` +0x166 | per-territory price-adjustment percent the order helpers mark base prices up by |
| **AI personality table** (`DAT_004d8a58`, const `.data`) | 4 personalities × 0xf0 B (= 3×0x50; retail indexes `(p-1)·0xf0`).  Embedded verbatim in `engine_army_ai.c`.  Reads: dispatch code (+0), offer band (+0x08), per-gift unit (+0x0c), tribute avg-troop floor (+0x30), cash floor (+0x78), discretionary buy qty (+0x7c), commodity 8/7/6 stock targets (+0x84/0x88/0x8c).  Values: pers1 code 1 (1000/100/250³), pers2 code 1 (1500/80/300³), pers3 code 0 (2500/70/500³), pers4 code 9 (4000/150/1000³) |
| **diplomacy inbox** (`s_slot_0053f0f0`, save entry 146, 240 B) | 6 rows (one per realm) × 5 entries × 8 B: +0 sender, +1 type (0..6), +2 byte param, +4 i32 value.  Drained + cleared by brain step 1.  Written by `FUN_004a2621(sender, receiver, type, value)` — **only two callers**: a human-UI diplomacy action (`DAT_0057c8cc` = local player) and an inbound MP message.  So in a headless SP AI-only sim it is **never populated** → step 1 is inert (see below). |
| per-pair AI fields (in `g_player_strategic_352` row p, `+ other·0x10`) | relation +0x84 (i8, ±30), contact/betrayal +0x86, alliance-exists +0x88, repeat-offer counter +0x89, best-offer-seen +0x8c (i32), per-turn scratch +0x90, gift counter +0x91.  Per-player scalars touched by step 1: avg-troop strength +0x14 (ranking output F_M_14), lord +0x81, last-gift-giver +0xe8, ally cooldown +0xec, mood-cycle +0x159 |

## Verification

Each function is host-unit-tested (28 tests in `test_engine_army_ai.c` +
`test_engine_territory.c`).  The transaction executor's arithmetic is isolated
by leaving the separately-bound economy/territory record pointers NULL so its
re-plan tail (provision/plan/grade/workforce) no-ops.

**No retail pure-function diff yet.**  The dispatchers run the realm's full
economy re-plan through `plan_realm`/`provision`, which read the live price
scalars `g_price_a/b/c` (fixed 5/10/6 in the diff lib, variable in retail) — the
same wall the upkeep pass hit ([pure-function-diff.md](pure-function-diff.md)
`upkeep` row).  A full-pass diff would need vectors constrained so the re-plan
tail stays deterministic; deferred as a separate effort.

## Brain step 1 — diplomacy inbox (`FUN_004a277d`, landed)

The per-tick brain dispatcher (`FUN_0049a581`) runs step 1 first for every realm
(state 0 init → state 1).  Step 1 = `engine_army_ai_step1_process_inbox`: it
walks the realm's five-entry inbox, dispatches each message to one of seven
type handlers, then clears the inbox (zeroing +0/+1/+2/+4, leaving +3 — same as
`FUN_004a2584`) and resets the per-pair +0x90 scratch byte.

| type | handler | effect |
|------|---------|--------|
| 0 | `FUN_004a29b2` | evaluate a resource offer vs the running best-offer (+0x8c) + a personality band; relation −8 / +5 / +10 by tier; post 0xad/0xac/0xab |
| 1 | `FUN_004a2cd6` | repeat-offer response keyed on +0x89; relation +15 / +8 / −4; post 0xae/0xaf/0xb0 |
| 2 | `FUN_004a2f61` | notified treaty break: `clear_treaty` + relation −20 + post 0xb1 |
| 3 | `FUN_004a30f6` | alliance-proposal response: reject 0xb2 / accept+`form_alliance` 0xb3 / has-lord 0xc5 / already-allied 0xc4 (probabilistic accept in the marginal band via `DAT_0058fd60`) |
| 4 | `FUN_004a3475` | silent treaty break: `clear_treaty` + relation −15 (no post) |
| 5/6 | `FUN_004a3551` / `FUN_004a38db` | tribute demand from one's lord: yield 0xb7/0xba / token-gift 0xb8/0xbb (`FUN_004a1b29`) / refuse 0xb9/0xbc; RNG-gated |

The relation byte is clamped to ±30 after every adjust (`>=31 → 30`, `<=-31 →
-30`).  The RNG input is the freshly-stirred 7-bit byte `DAT_0058fd60`
(`g_rng_7bit_b`), threaded from `engine.c` — the sub-step body stirs it
(line 2028) *before* the strategic dispatch (line 2048), exactly retail's
`FUN_004b99c0` order, so the value is byte-exact.

**Verification.**  `FUN_004a277d` + `FUN_004a29b2` are r2-verified byte-for-byte
(dispatch address `0x53f0f0 + self·0x28 + entry·8`; sender==0 breaks,
sender==self continues; personality read `0x4d8a60` = row+0x08; codes/clamp
constants).  Five host tests in `test_engine_army_ai.c` exercise every handler
path (offer warm, treaty break, alliance accept, self-skip + gap-stop, empty
drain).  **The trace-studio oracle CANNOT reach step 1** — the inbox is filled
only by human-UI/MP diplomacy (see the inbox row above), so a headless SP
`endturn_*` capture leaves it empty and step 1 is provably inert (a before/after
`--only port` on `endturn_auto` is byte-identical).  Exercising it on-engine
needs a diplomacy-UI trace (human proposes/breaks/tributes to an AI realm).

## Brain step 9 — offensive-campaign planner (`FUN_0049f977`, landed)

The per-tick brain dispatcher runs step 9 as the AI's persistent army-campaign
driver (dispatch state 9).  Ported 2026-06-10 as
`engine_army_ai_run_campaign` + six closure functions (all in
`engine_army_ai.c`).  Unlike step 0xa (a one-shot, cooldown-gated raid) it carries
a multi-turn campaign target in `+0xe8` and sizes the raised force against a
personality "force budget".

| fn | role |
|----|------|
| `FUN_0049f977` | entry: tick patience (+0x45 vs personality +0x28) → commit/validate a target → raise (muster) or redirect (pick_best_unit) a force → assign it |
| `FUN_0049ff71` | validate a committed target: drop if the player has no lord, the target is the lord's own undefended realm, or the target is self-owned |
| `FUN_004a01ea` | set the target-owner gate (+0x48): a pinned enemy, else the field leader (rank < 2) if foreign/active/high-gauge |
| `FUN_0049fcc5` | easy-AI funding cheat: personalities 3/4 below the late-era cutoff bank +100 into the funding reserves; returns whether it funded |
| `FUN_0049fda5` | stamp the chosen unit with the march order (code 2, or 6 when the target is the lord's realm), clear placed, pick a deploy cell |
| `FUN_004a65a3` / `FUN_004a6735` | find the nearest owned cell to the unit (flag 0x80 + cropland terrain 0x15..0x19 / flag 0x40), leaving col/row in the find-nearest scratch (DAT_00569570/78); fall back to the realm's settled/capital anchor cell |

The pathfinder/deploy closure (`FUN_0046f700` group) was already ported in
`engine_path_field.c`; the muster (`FUN_004a50ae`), unit-pick (`FUN_004a0917`)
and target-select (`FUN_004a03f2`) callees were already ported in
`engine_combat.c` / `engine_army_ai.c`, so step 9 added only the six functions
above (~1.9 KB).

**Personality-table fields newly mapped** (offsets into a `personality_row`):
`+0x28` campaign period (3/4/4/2), `+0x30` force budget (750/800/900/1000 — the
SAME byte as the step-1 tribute floor, reused), `+0x40` muster percent
(30/30/40/50), `+0x68` force-credit floor (100/120/200/250).

**Verification.**  `FUN_0049f977`/`FUN_0049fda5`/`FUN_004a65a3` are r2-verified
byte-for-byte (the entry's target/pinned/patience branch structure with
`cmp [+0x4d8a80], tick; jle`, the budget `sar 1` halvings, the +0x4b commit; the
assigner's order codes 2/6 at `0x52f0ca` + the keeplvl/pending finder dispatch;
the finder's flag `0x80`/terrain `0x14<t<0x1a` gates).  Six host tests in
`test_engine_army_ai.c` cover the patience tick, the self-owned-target drop, two
deployment paths (capital fallback + settled-cell finder, order codes 2 and 6),
the muster-branch force-floor gate, and the dispatcher wiring.

**Unlike step 1, the trace-studio oracle DOES reach step 9.**  A before/after
`--only port` on `endturn_auto` (same pinned RNG seed; the only change is step 9
stub vs live) shows the port's `g_player_strategic_352` change at the run2/run3/
settled labels — by then the AI realms' patience counters fire and step 9 starts
committing campaign targets.  It does NOT yet byte-match retail there: the
documented ±3-4 pass-counter timing offset (auto_dismiss onEnter vs the port's
input phase) skews the snapshot instants, and steps 6/7/0xb are still stubbed —
the run-label strategic sub_diff `[5,7,8,9,10,11,12,13,15]` is that combined
residual, not a step-9 defect.

## Quirks captured in-code

- `FUN_004a4782` backward tax sweep resets `+0x15f` to the best rate **inside**
  the loop (dead — overwritten next iteration) as well as after; kept faithfully.
- `FUN_00428c42` approval cap uses the `'c'` (99) threshold, not 100.
- personality index has no retail bounds check; the port clamps `p` to 1..4 to
  avoid reading past the embedded image array (never triggers in real play).
