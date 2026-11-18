# New-game save parity — port `--newgame` vs retail (residual divergences)

Status as of 2026-06-03 (after the **turn-driver global-RNG threading fix** —
see #3/#4/#5 below).  The new-game **turn-spam bug is fixed** and a retail load
of `port_newgame.sav` is stable (user-confirmed).  This note records the
**remaining byte divergences** between the port's new-game save and a same-seed
retail new-game save, with root causes, for future resolution.  None affect the
turn-spam fix or human-player playability.

**2026-06-03 update — one fix collapsed five entries.**  The whole-save diff vs
`lastturn.sav` dropped from **75 B → 34 B**, and of the residual 34 B, **20 B are
save-point artifacts** (entries 124/150/151/152/159–164, below) — leaving **only
entry 3 (14 B, AI strategy +0x130)** as a genuine divergence.  The fix: retail's
new-game seeder **FUN_00497e65 (seat_shuffle) stirs the GLOBAL double-LFSR**
(`DAT_0058fe28/0058fe08`) via FUN_00404a46; the port threaded its draws through a
*local* copy seeded to the same InitInstance constants, so the global never
advanced and the turn driver's stir (step 17) started from the wrong state.
Threading the global through seat_shuffle (`engine_rng_global_get`/`_set`, and
mirroring the live state into the save home) fixed entries **0, 4, 18, 19, 79**
at once — they were all downstream of that one global-RNG divergence, NOT the
separate causes the earlier draft assigned them (see the per-entry notes).

**2026-06-03 update #2 — entry 3 closed; the new-game strategic block is now
byte-exact vs a point-matched retail dump (0 B).**  The previously-"genuine"
entry-3 residual was two unrelated things the raw `lastturn.sav` diff conflated:
- **+0x130 (4 B, players 2–5) — REAL bug, now FIXED.**  A **scalar-wall split**:
  retail's per-group resource-demand pool `DAT_0057c020` (stride 0x160)
  physically **lives inside** the player-strategic block at `+0x120` — the
  commodity pools `+0x120/+0x128/+0x130` *are* the resource accumulators, viewed
  through two bases (the economy pass via `DAT_0057c020`, the AI brain via the
  strategic record).  The port split them: the strategic block was a real buffer
  but `engine_army_economy_bind_resource_pool` was only ever called in tests, so
  the live `g_resource_pool` stayed **NULL** and the turn-1 resource-delivery
  pass (`apply_resource_quota` → `respool_field(group, RESPOOL_A_FLAG0) +=
  amount`, i.e. strategic `+0x130`) was silently dropped — `+0x130` stayed at the
  config default `0x32`.  **Fix:** bind the pool to alias the strategic block in
  `engine_game_init` — `engine_army_economy_bind_resource_pool(g_player_strategic_352
  + 0x120)`, exactly as retail overlaps them.  `+0x130` now produces
  `0x32/0x74/0x74/0xb6/0xd8` byte-exact.  (Recurring scalar-wall smell — cf.
  #175/#176/#178 and the RNG global threading above.)
- **+0x2b (rank ×5) and +0x50 (score ×5) — NOT divergences; save-point
  artifacts.**  These are ranking outputs: `F_RANK = 0x2b`, `F_SCORE = 0x50`,
  written by `engine_ranking_recompute` (FUN_0049aa0e) at orchestrator **step
  20–21**, *after* the **step-19** `save_rotate` that produces retail's
  `lastturn.sav`.  So `lastturn.sav` has them at 0 (ranking hadn't run); the
  port's save is taken after the full init (step 26) and carries the computed
  rank/score.  Same nature as entry **150** (the compact ranking board) — already
  a confirmed artifact.  **Verified:** a point-matched frida dump of the whole
  `0x57bf00:2112` strategic block (driven by `new_game_to_map.jsonl`, `--rng-seed`)
  is **byte-for-byte identical to `port_newgame.sav` — 0 differing bytes**,
  rank/score included.  So the port is correct; only the raw `lastturn.sav` diff
  (a step-19 snapshot) shows them.

Net: the raw `lastturn.sav` diff is now **30 B across 11 entries, and every one
of those 30 B is a step-20…26 save-point artifact** (entry 3 +0x2b/+0x50, 124,
150, 151, 152, 159–164).  The new-game save is byte-exact for the entire state
that exists at retail's step-19 save point, and the strategic block matches a
point-matched step-26 dump with **zero** differences.

## Method

Both sides use map 0 + the canonical RNG seed (`--rng-seed`), a default custom
game (1 human + 4 AI):

- **port**: `/tmp/ol2.exe --newgame 0` → `port_newgame.sav` (written after the
  full init, i.e. retail step 26).
- **retail**: drive `tools/traces/new_game_to_map.jsonl` under
  `frida_capture.py --rng-seed`; retail writes `lastturn.sav` during new-game
  setup (the step-19 save-rotate seam 0x49a453).  Run `newgame-retailsave1`.
- **point-matched truth**: `frida_capture.py --dump-regions/--dump-globals` at
  the first MAP_FREEROAM frame (run `newgame-fulldump1`) — the live state after
  the *full* init, the same logical point as the port's save.

Tool: `tools/save_diff.py A.sav B.sav` (schema-aware, maps each differing byte
to its schema entry + retail VA).

**Save-point caveat:** retail's `lastturn.sav` is written at step 19, the port's
save at step 26, so a raw save-vs-save diff over-counts.  Entries
**124, 150, 151, 152, 159–164** differ in the save-diff but *match the live
dump* — the port is correct there; they are step-20..26 outputs retail's
step-19 save simply hasn't written yet.  The list below is the **point-matched**
set (port vs live `newgame-fulldump1`), i.e. the genuine divergences.

## Real divergences (point-matched)

### 1. Category-bucket table not persisted — FIXED (entries 138/139) + cascade RESOLVED (entries 78 + 5)
**STATUS:** the bucket cluster is byte-exact AND the placement cascade is now
resolved (entry **78** count = 6, entry **5** capital records match retail).
The cascade was NOT an unported populator (the original quirk #174 was wrong) —
it was two port bugs: (a) `army_near` misread the coord stride as `3` instead of
`0x300` — the probe addressing has a `shl eax,8` that makes it
`g_per_army_768[id*0x300 + 0x6c]` (record `id`'s capital cursor, written by the
already-ported `setup_capitals`); and (b) `engine_map_query`'s cell grid was
never bound in `src/` (only in tests), so the radius probe always early-returned
on `g_cells==NULL`.  Fixing both: 251→**101 B** total, 1871/1871 tests green.
See engine-quirks #174 (corrected) + #175.

**Original bucket-fix note (still accurate for entries 138/139):** the bucket
cluster is byte-exact.
`engine_game_init` mirrors the computed `g_gi_strat_buckets` into its two
save-schema homes at a new step **6b** (after `setup_overworld` finalises the
digest, before step-8 `place_army_starts` reads it) — the two retail globals
are non-contiguous so the single struct can't overlay both.  Result: entries
**138/139 dropped out of the diff** (48 B recovered, 299→251 B; verified via
`save_diff` vs `lastturn.sav`, all 1871 host tests green).

The predicted **cascade (entry 78 + entry-5 capitals) did NOT follow** — it has
a *separate* root cause, see the box below.  Original analysis kept for context:

- **Entries 138 (`0x567970`, 96 B) + 139 (`0x569518`, 6 B)** were entirely zero
  in the port save, fully populated in retail.
- These are the strategic **category-bucket table**: `buckets[6][16]` (entry
  138) + `digest[6]` (entry 139), per `engine_category_buckets_state_t`.
- Cause: `engine_game_init` binds the cluster to a **private**
  `g_gi_strat_buckets` (game_init.c ~984/1016 `engine_territory_bind_category_buckets`)
  instead of the shared save-schema homes `s_slot_00567970` / `s_slot_00569518`.
  Classic scalar-wall (see project_shared_save_globals): the live computation is
  correct but lands in a buffer the save serialiser never sees.
- **Cascade** → `engine_game_init_place_army_starts` (game_init.c:548, FUN_00427ed0)
  reads the army-id list at entry 139; finding it empty it places **no** start
  markers, so:
  - entry **78** (`0x5530b4`, placed-army count) = 0 vs **6**;
  - **entry 5 per_territory** (`0x52f0b0`, ~131 B) — the capital records'
    `+0x150/+0x10/+0x164/+0x166/+0x167/+0x14f` stay unstamped;
  - likely some of **entry 4 per_army** too.
- **Fix (DONE):** mirror the cluster into the two save-schema homes at init
  step 6b (post-`setup_overworld`).  Recovered entries 138/139 only.

> **Cascade root cause — RESOLVED (engine-quirks #174 corrected + #175).**  With
> the digest correct, `place_army_starts` (step 8) still placed **0** until two
> port bugs were fixed: (1) `army_near` read the coord at a bogus packed stride
> `id*3`; the probe addressing has a `shl eax,8` making it `id*0x300 + 0x6c` =
> record `id`'s capital cursor (stride `0x300`), written by the **already-ported**
> `setup_capitals` — so there was no unported populator.  (2) `engine_map_query`'s
> `g_cells` was never bound in `src/` (only in tests), so the radius probe always
> early-returned on `NULL`.  After both, all 6 armies place; entries **78** and
> **5** match retail.  The "packed stride-3 / unported computed-base store" theory
> was a misread of the probe addressing.

### 2. Human player name not copied — Entry 2 (`0x553d50`, 17 B) — FIXED
- `player_aux` record 1 holds the retail config-screen name **and** kingdom/title:
  `+0x04..+0x0d` = the player name (`"aaaaaa    "`), `+0x1c..+0x22` = the title
  (`"castles"`).  The port left the whole record zero.
- Root cause traced end-to-end: retail's bootstrap (`0x4ae158`) reads
  **`lords2.inf`** (0x468 B) verbatim into the config buffer `DAT_0053f1e0`; that
  blob carries the **name at +0x00** and the **title at +0x18**.  The SP seat
  (`0x4410bd` → `player_seat_assign` `0x49bae9`) then copies a **31-byte** name
  field from `0x53f1e0` into the aux record at `+0x04` — so the name lands at aux
  `+0x04` and the title at aux `+0x1c` (the `+0x18` shift).  Both strings come
  from one 31-byte copy out of the persisted prefs; NOT a hard-coded default.
- **Fix (DONE):** port the prefs load (`config.c load_lords2_inf` → buffer +
  `config_player_prefs()` accessor, faithful to `0x4ae1b8`) and pass that buffer
  to `engine_game_init_set_player_config`, which copies the 31-byte name into the
  human seat's aux `+0x04` (NULL = file absent → left zero).  Also seeds entry 86
  (`0x553f98`, human/active count) = `human_count` (retail's `0x4410bd inc`).
  Verified: entry 2 (17 B) and entry 86 drop out of `save_diff` →
  **101 B → 83 B, 26 → 24 entries**; all 1871 host tests green.

### 3. Entry 0 (`0x522f90`) — realm-standing markers — **FIXED** (was mis-filed "autotile")
- **RE-ATTRIBUTED + FIXED 2026-06-03.**  The 9-B residual was **NOT** an autotiler
  build issue.  Per-pass instrumentation of `setup_overworld` showed the autotiler
  leaves these cells untouched; the divergence is 3 **single** cells (273→realm
  0xe, 601→0xb, 917→0xc) that retail sets to type **0x17** and the port left at
  the `seed_army_features` type 0x14.  Type 0x17 is written by
  `engine_realm_update_mood` (FUN_00449889) → `realm_mark_next_cell(key,0x17)` —
  the **band-5 (worst-mood) realm standing marker**, run by the turn driver
  (step 17).  Retail classified realms 0xe/0xb/0xc as band 5 and marked one cell
  each; the port did not, because its turn-driver `update_mood` saw the wrong RNG
  state (focus realm `sel = rng_a & 0xf` and the per-realm mood deltas both
  diverged).  Root cause = the global double-LFSR not advanced through
  `seat_shuffle` (see the header + #5).  Fixed → entry 0 byte-exact.
  (The matching 2×2 type-0x17 blocks, e.g. 797/798/861/862, are the unrelated
  *secondary-keep* footprints — those already matched.)

### 4. AI economy — Entry 4 (`0x53f9b0`) — **FIXED**; Entry 3 (`0x57bf00`) — **FIXED**
- **Entry 4 per_army** (`0x53f9b0`, was ~23 B): AI realm economy fields — **FIXED**
  by the global-RNG threading fix.  These are produced by the turn driver's AI
  economy passes (provision / plan_realm / season passes); once the turn driver
  ran from the correct RNG state they matched byte-exact.
- **Entry 3** (`0x57bf00`) — **FIXED 2026-06-03 (update #2 above).**  The 14 B
  split into a 4 B real bug + 10 B save-point artifacts:
  - `+0x130` (players 2–5): the resource-demand pool `DAT_0057c020` was never
    bound (NULL), so the turn-1 resource pass dropped its `strategic+0x130 +=
    amount` accumulation.  Bound it to alias the strategic block at `+0x120` →
    byte-exact (`0x32/0x74/0x74/0xb6/0xd8`).
  - `+0x2b` (rank) / `+0x50` (score): ranking outputs (`engine_ranking_recompute`,
    step 20–21) that post-date retail's step-19 `lastturn.sav`; not a divergence.
  Point-matched frida dump of the full `0x57bf00:2112` block vs `port_newgame.sav`
  = **0 differing bytes**.  Nothing left on the new-game strategic block.

### 5. Misc bookkeeping scalars — mostly FIXED
- **Seeded (DONE):** entries **73** (`0x569584`=1), **75** (`0x53f06c`=7),
  **76** (`0x53f678`=4), **84** (`0x57c944`=1) — all written at the **top** of
  the config-screen player-setup routine the port already reproduces (retail
  **FUN_0049792e** @ 0x49792e, before its player loop + npc-schedule call), so
  they were added to `engine_game_init_set_player_config`.  Plus **207**
  (`0x53f0d4`=0xa, retail FUN_00497a34 @ 0x497abc) and **197** (`0x55302c`=2, the
  SP "full-game" substate) seeded in the `--newgame` seat (`bringup_newgame`).
  Entry **86** (`0x553f98`) handled in #2.  Entry **78** (`0x5530b4`) was the
  placement cascade — already resolved in #1.
- **Remaining (not clean constants, deferred):**
  - **79** (`0x554020`): retail 0x0b, port 2 — **FIXED 2026-06-03.**  This is NOT
    a config-screen selection cursor (the earlier guess); it is the realm-mood
    **focus realm** `sel`, written by `engine_realm_update_mood` (FUN_00449889)
    at `s_slot_00554020 = sel` after `sel = rng_a & 0xf` (+ fallbacks).  It
    diverged for the same reason as entry 0: the turn-driver `update_mood` saw
    the wrong global RNG state.  Fixed by the global-RNG threading fix → 0x0b
    byte-exact.
  - **16/17** (`0x5651b4`/`0x5651b8`): the camera scroll col/row — **FIXED**.
    The recenter-on-capital (`engine_territory_recenter_on_capital`
    `FUN_00432746` → `_recenter_on_tile` `FUN_0043278b`, run by `setup_realms`)
    already computed the capital-centred scroll **24/2** into the save globals
    (verified live: `DIAG recenter scroll s_slot col=24 row=2`).  The bug was a
    **scalar-wall storage split**: retail has ONE scroll storage
    (`DAT_005651b4/b8`) read by both the renderer and the save, but the port
    splits it (the renderer reads `engine_map_init`'s `g_col_start/g_row_start`
    statics — left at the map-init default 20/74 — while the save reads the
    `s_slot` mirror).  The `--newgame` seat had *clobbered* the recenter output
    back to the render default (20/74) to keep save==render, picking the wrong
    winner.  **Fix:** bridge the other way — push the recenter'd `s_slot` scroll
    into the render camera via `engine_map_init_set_scroll`, exactly as the LOAD
    path (`engine_map_post_load`) already does.  Now the first new-game frame
    is centred on the player's capital (matching retail) AND the save carries
    24/2 byte-exact.  Entries 16/17 drop from the diff → **77 B → 75 B,
    18 → 16 entries**.  See engine-quirks (camera scroll-storage split).
  - **18/19** (`0x58fe28`/`0x58fe08`): the **RNG state** (`g_rng_state_a/b`) —
    **FIXED 2026-06-03.**  Two bugs, both addressed: (a) the live state was never
    mirrored into the save-schema globals (classic scalar-wall) — now
    `engine_tick_rng_step` + `engine_rng_global_set` write `s_slot_0058fe28/08`
    on every advance; (b) the state didn't reach retail's `abf3a863`/`e902a252`
    because the port left the **global** at its InitInstance seed — the init
    RNG-consumer chain (just `seat_shuffle`, FUN_00497e65, which stirs the global
    3×/iteration via FUN_00404a46) was running on a *local* copy.  Threading the
    global through `seat_shuffle` advances it exactly as retail does; the turn
    driver's stir (step 17) then lands on retail's value → 18/19 byte-exact.

## Not divergences (save-point artifacts — port already correct)
Entries **124, 150, 151, 152, 159, 160, 161, 162, 163, 164** match the live
dump; they only appear in a raw save-vs-save diff because retail's `lastturn.sav`
is written at step 19, before these step-20..26 outputs.

## Reproduce
```
# port save
cp build/openlords2-debug.exe /tmp/ol2.exe
( cd "$GAMEDIR" && /tmp/ol2.exe --newgame 0 --frames 5 --no-cd --allow-multi )

# retail same-seed new-game save + point-matched live dump
nix develop -c python3 tools/frida_capture.py --remote $OPENLORDS2_FRIDA_REMOTE \
  --skip-intro --turbo --rng-seed --input-segtrace tools/traces/new_game_to_map.jsonl \
  --run-id rs --dump-regions "0x522f90:32768:descriptors,0x553d50:264:player_aux,\
0x52f0b0:63420:per_territory,0x57bf00:2112:player_strategic,0x53f9b0:13056:per_army,\
0x567970:96:p138,0x569518:6:p139,0x565410:48:p150"

nix develop -c python3 tools/save_diff.py "$GAMEDIR/lastturn.sav" "$GAMEDIR/port_newgame.sav"
```
**#1 (bucket cluster + placement cascade) and #2 (name/title) are fully DONE,
#5 is FIXED** (73/75/76/84/197/207/86 seeded; 16/17 camera; 18/19 RNG; 79 focus
realm), **entry 0 (#3), entry 4 (#4) AND entry 3 are FIXED** (global-RNG
threading + resource-pool binding).  Total save-diff vs `lastturn.sav`: **30 B**
across 11 entries (down from 251 → 101 → 83 → 77 → 75 → 34 → **30**), and **all
30 B are now save-point artifacts** — every one is a step-20…26 output that
retail's step-19 `lastturn.sav` predates (entry 3 +0x2b/+0x50 ranking, 124, 150,
151, 152, 159–164).  **No genuine new-game divergence remains.**

So **the entire new-game save is byte-exact for the state that exists at retail's
step-19 save point**, and a point-matched step-26 frida dump of the player-
strategic block (which held the last genuine residual, +0x130) matches the port
**byte-for-byte (0 B)**.  The new-game camera frame is byte-exact + visually
confirmed (first strategic frame recentres on the player's capital).

### The fix (2026-06-03) — global double-LFSR threading
Retail's new-game seeder **FUN_00497e65 (seat_shuffle)** stirs the **global**
double-LFSR `DAT_0058fe28/0058fe08` via FUN_00404a46 (3 advances per loop
iteration; it is the *only* init consumer of that global before the turn driver).
The port had ported seat_shuffle to thread a **local** `engine_rng_in_t` seeded
to the same InitInstance constants — so its *output* (the shuffle + the two
48-entry tables) matched, but the global never advanced.  The turn driver
(step 17) reads that global, stirs it once (`engine_tick_rng_step`), and feeds
`rng_7bit_a/b` to every RNG-driven turn-1 pass (realm mood/standings, focus
realm, AI economy).  Starting from the un-advanced seed perturbed all of them.

Changes:
- `engine.c`: `engine_rng_global_get/_set` accessors; `engine_tick_rng_step` and
  `_set` mirror the live state into `s_slot_0058fe28/08` (save entries 18/19),
  collapsing the scalar-wall split (one storage, as retail).
- `game_init.c`: load the global in (`engine_rng_global_get`), thread it through
  `engine_game_init_seat_shuffle`, write the advanced state back
  (`engine_rng_global_set`) — exactly the side effect retail's seat_shuffle has.
- Tests: host stubs for the two accessors + a per-test seed reset (retail seeds
  the global once at InitInstance/FUN_004b2258, never per new-game).
