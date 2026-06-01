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
| **AI personality table** (`DAT_004d8a58`, const `.data`) | 4 personalities × 0xf0 B (= 3×0x50; retail indexes `(p-1)·0xf0`).  Embedded verbatim in `engine_army_ai.c`.  Reads: dispatch code (+0), cash floor (+0x78), discretionary buy qty (+0x7c), commodity 8/7/6 stock targets (+0x84/0x88/0x8c).  Values: pers1 code 1 (1000/100/250³), pers2 code 1 (1500/80/300³), pers3 code 0 (2500/70/500³), pers4 code 9 (4000/150/1000³) |

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

## Quirks captured in-code

- `FUN_004a4782` backward tax sweep resets `+0x15f` to the best rate **inside**
  the loop (dead — overwritten next iteration) as well as after; kept faithfully.
- `FUN_00428c42` approval cap uses the `'c'` (99) threshold, not 100.
- personality index has no retail bounds check; the port clamps `p` to 1..4 to
  avoid reading past the embedded image array (never triggers in real play).
