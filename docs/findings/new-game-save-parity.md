# New-game save parity — port `--newgame` vs retail (residual divergences)

Status as of 2026-06-02 (after the config-screen player-setup fix, engine-quirks
#173 / commit a8eee4c).  The new-game **turn-spam bug is fixed** and a retail load
of `port_newgame.sav` is stable (user-confirmed).  This note records the
**remaining byte divergences** between the port's new-game save and a same-seed
retail new-game save, with root causes, for future resolution.  None affect the
turn-spam fix or human-player playability.

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

### 1. Category-bucket table not persisted — FIXED (entries 138/139); cascade is a SEPARATE root cause
**STATUS (commit landing this update):** the bucket cluster is now byte-exact.
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

> **Cascade root cause — NOT the buckets (engine-quirks #174).**  With the
> digest now correct, `place_army_starts` (step 8) still places **0** (retail
> places 6): its probe reads each army's coord from a **packed stride-3** table
> `coord[id*3]` based at `0x53fa1c` (= `g_per_army_768 + 0x6c`), and for every
> digest id that read returns `(0,0)` (diag-build confirmed) → both rawflag
> probes fail → nothing placed, so entry 78 stays 0 and the entry-5 capital
> records stay unstamped.  This packed table is **distinct** from the stride-
> `0x300` capital cursors `setup_capitals` writes to the same base, and is
> populated by **no ported pass** (ruled out: `load_strat_map`, `setup_capitals`,
> roster).  Retail fills it before step 8 via a computed-base store (transient —
> record-0's `+0x6c` already matches retail in the save).  **Next step:** frida
> mem-watch `0x53fa1c` across a retail new-game to identify the populator; it
> belongs to the per-army-setup frontier, not this bucket fix.

### 2. Human player name not copied — Entry 2 (`0x553d50`, 17 B)
- `player_aux` record 1 `+0x04..+0x23` (the 31-byte name) holds the retail
  config-screen name (`0x61 'a' …`) ; the port leaves it zero.
- `engine_game_init_set_player_config` seeds the aux **job reservation**
  (`+0x25/+0x27`) but not the name string that retail's SP `player_seat_assign`
  copies (from the config name buffer `0x53f1e0`).
- **Fix (trivial, cosmetic):** seed the default name into `g_player_aux_44[1*0x2c + 4..]`.
  Also seed entry 86 (`0x553f98`, human count) = 1 (retail's 0x4410bd `inc`),
  currently 0.

### 3. Descriptor autotile-build residual — Entry 0 (`0x522f90`, 37 B)
- The new-game descriptor **build** (`setup_overworld` → `classify_feature_tiles`
  0x46dbf5 / `randomise_special_tiles` 0x46dd34, the single-LFSR autotiler)
  produces a handful of cells differently — 3 cells of field **+0** (type) plus
  **+2/+3** (render-flags / sprite) on a few border/special cells.
- Same family as the known autotile near-miss (`test_map_finalize` reports
  +2≈99.5%).  NOT the LOAD path, which deserialises descriptors and matches
  100% — this is build-only.
- **Fix:** an autotiler RNG/order residual; defer with the other autotile work.

### 4. AI economy / strategy residual — turn-driver/AI frontier
- **Entry 3** (`0x57bf00`): 4 B — the AI players' `+0x130` strategy field
  (port 0x32, retail 0x74/0x74/0xb6/0xd8).
- **Entry 4 per_army** (`0x53f9b0`): ~90 B — AI realm economy fields
  (`+0x01/02/03`, `+0xb9`, `+0x1a0`, `+0x21d`, …).
- Remaining **entry 5** beyond the cascade in #1.
- These are AI-realm economy/strategy values produced by the turn-driver's AI
  passes; the documented turn-driver/AI byte-completeness frontier
  (project_next_chip_4485a5).  Not a new-game-specific bug.

### 5. Misc bookkeeping scalars
- `0x569584` (73)=1, `0x53f06c` (75)=7, `0x53f678` (76)=4, `0x5530b4` (78)=6,
  `0x554020` (79)=11, `0x57c944` (84)=1, `0x553f98` (86)=1, `0x55302c` (197)=2,
  `0x53f0d4` (207)=10 — port mostly 0.  Some cascade from #1 (76/78 are
  placement-adjacent); 197 (`0x55302c`) is the SP "full-game" substate the live
  menu path sets (engine_menu_input.c) but the headless `--newgame` seat skips.
  Mostly one-line seeds; low priority.

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
nix develop -c python3 tools/frida_capture.py --remote cutestation.soy:27042 \
  --skip-intro --turbo --rng-seed --input-segtrace tools/traces/new_game_to_map.jsonl \
  --run-id rs --dump-regions "0x522f90:32768:descriptors,0x553d50:264:player_aux,\
0x52f0b0:63420:per_territory,0x57bf00:2112:player_strategic,0x53f9b0:13056:per_army,\
0x567970:96:p138,0x569518:6:p139,0x565410:48:p150"

nix develop -c python3 tools/save_diff.py "$GAMEDIR/lastturn.sav" "$GAMEDIR/port_newgame.sav"
```
Suggested order of attack: **#1 (buckets) → #2 (name) → #5 → #4/#3** (the last two
ride existing autotile / turn-driver frontiers).
**#1's bucket half is DONE**; its placement cascade (entry 78 + entry-5 capitals)
now rides the unported coord-table populator (engine-quirks #174), which is
per-army-setup-frontier work, not a bucket change.
