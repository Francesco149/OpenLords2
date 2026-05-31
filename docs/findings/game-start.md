# Game-start — from the menu into a live game

How the single-player **Start** button transitions out of the title
menu and stands up a live world.  This is the gateway milestone behind
page-7 Start, page-4 Play-Now Continue, and page-3 Load.

## Entry chain

```
page-7 "Start" button
  → FUN_004335f0 @ 0x4335f0   (carved Cancel/Start/Defaults handler; Start = DAT_0059154c==2)
       if (DAT_00553f98 <= DAT_0053e8a4) {        // player-count fits the roster
         FUN_00499dc3();                           // 0x499dc3 (154 B) finalize roster
         FUN_004329ec();                           // 0x4329ec (281 B) game-start dispatch
         DAT_0052afa4 = 1;                          // request the strategic-map enter
       }
```

`FUN_004329ec` (the dispatcher), single-player arm (`DAT_00553030 == 0`,
`DAT_005aeb8c == '\0'`):

```
FUN_00497ced();        // 0x497ced (376 B) — the new-game world initialiser
DAT_004eac50 = 0;      // screen-id ← 0
DAT_0052af94 = 3;      // engine state ← 3 (INGAME)  ← the mode flip
FUN_00451f8a();        // 0x451f8a (66 B) — enter-game / first-frame kick
...
_DAT_00568d9c = timeGetTime();
```

The multiplayer arm (`DAT_00553030 != 0`) instead runs
`FUN_00499a1c(0/1)` + `FUN_0043eda0(4,0)` and is deferred.

## The screen-dispatch wall

The top-level per-frame renderer is `FUN_0040f1a0`
(`src/engine_screen_dispatch.c`), keyed on the **screen-id**
`DAT_004eac50`.  Only the title arm (`0x1f`) is ported; the in-game /
strategic-map arms (`FUN_004148xx … FUN_0040f5fd`) all funnel through
`other_stub`.  So a **visible** in-game map needs *both*:

1. the world-state init tree (this doc), to populate the tables the map
   renderer reads, and
2. the strategic-map screen-arm of `FUN_0040f1a0`, still unported.

Flipping `DAT_0052af94 = 3` alone does **not** render a map.

## Scope: the transitive closure is the whole engine

BFS over `{FUN_00499dc3, FUN_004329ec, FUN_00451f8a}` reaches
**1217 functions / 533 KB** (933 unported / 424 KB) — essentially the
rest of the engine, because game-start → init → the full downstream sim
and render.  **Game-start is therefore not a single unit; it is built
incrementally**, and the closure number is not the milestone size.

The tractable target is the **init itself**, not the closure.
`FUN_00497ced` has **25 direct callees** (20 unported), a finite set of
seeders that run once at new-game and mostly do not recurse into the
live sim:

| callee | size | callees | role (provisional) |
|--------|-----:|:-------:|---------------------|
| FUN_0046f3cd | 78 | 1 | clear per-territory block (151×420) ✅ |
| FUN_0046f2ae | 67 | 1 | clear unit-table block (13×20) ✅ |
| FUN_0046f1dc | 67 | 1 | clear per-army block (17×768) ✅ |
| FUN_0049caaa | 935 | 2 | NPC schedule seed — **already ported** |
| FUN_00427ed0 | 311 | 3 | ✅ army start-marker placement (→ entity placer FUN_0046e1b0) |
| FUN_0042847c | 82 | 0 | copy DAT_004d8910 table → DAT_005532a0 strided |
| FUN_00448440 | 261 | 29 | (unsurveyed — large fan-out) |
| FUN_00451150 | 936 | 7 | (unsurveyed) |
| FUN_004676e0 | 144 | 11 | ✅ **ported** — overworld-grid setup sequence (`engine_territory_setup_overworld`); 10/11 callees real, the 11th (0x46a037 country-data bind) deferred at its call site |
| FUN_0046f1dc/2ae/3cd | — | — | (the trio above) |
| FUN_0046ff43 | 617 | 0 | leaf seeder (unsurveyed) |
| FUN_00472ad0 | 16 | 1 | **tested** (already ported) |
| FUN_00497e65 | 1025 | 1 | per-player RNG seat assignment |
| FUN_004984dc | 731 | 9 | (unsurveyed) |
| FUN_00499aca | 487 | 2 | (unsurveyed) |
| FUN_00499d7d | 26 | 1 | play click3.wav (audio) |
| FUN_0049a453 | 302 | 2 | **tested** (already ported) |
| FUN_0049aa0e | 2197 | 1 | (unsurveyed — large) |
| FUN_0049b35e | 205 | 1 | ✅ **tested** — purge inactive players' strategic records |
| FUN_0049bc5f | 314 | 0 | **tested** (already ported) |
| FUN_0049bd99 | 3063 | 9 | (unsurveyed — largest) |
| FUN_004a1c53 | 537 | 2 | ✅ **tested** — reset per-player sub-tables (entry 146 grid + 5-entry per-player sub-table) |
| FUN_004ac904 | 295 | 1 | ✅ **tested** — unit/opponent-type table seeder |
| FUN_004ae74d | 144 | 0 | clear DAT_0056d8b8 array (400×0x20) + 3 scalars |
| FUN_004ae8a9 | 191 | 1 | **tested** (already ported) |
| FUN_004b18f8 | 38 | 0 | PostMessage(WM_MOUSEMOVE) self-poke |

5 of the 25 are already ported (npc-schedule + 4 tested).

## The scalar-global wall is NOT a blocker here

`FUN_00497ced`'s body writes ~20 scalar world-state globals
(`DAT_00553edc = 0x4f3`, `DAT_0057c934 = 3`, …).  17 of them are save
fields — but every one already has a **shared `s_slot_<va>` storage
home** in `src/save_schema_globals.h` (the runtime-global ownership
model the 2026-05-29 wall was waiting on; it shipped).  Game-init seeds
them exactly like `engine_npc_schedule.c` already seeds
`s_slot_0053f268` / `s_slot_0055302c` / `s_slot_0053f034`: include the
header, write the slot.  No desync, no second copy.  (Verified
2026-05-30: all 17 present as `s_slot_<full-va>` externs.)

### The 3 non-save "free" DATs — home decision resolved (2026-05-30)

Two of the three were **already homed** (just unreachable): `DAT_00553fc8`
and `_DAT_0056d658` were `engine.c` file-statics (`g_sim_turn_progress` /
`g_sim_turn_sync_baseline_ms`).  Since `engine.c` is deliberately out of the
host test suite (it drags in `mmsystem.h`/`gfx.h`/the main loop), routing
game-init through it would break testability — and re-declaring them in
`game_init.c` would be a second copy (a desync risk, the same trap the save
scalars avoid).  Resolution: **relocate their storage into `engine_turn.c`**
(the turn module — already test-linked, light deps; `engine.c` still owns the
live per-tick read/write and reaches them via the `engine_turn.h` extern it
already includes).  The third, `DAT_0053f0c4`, is genuinely unhomed (a
render/anim gate counter, sole reader `0x41e1dd`); it is parked in
`game_init.c` as `g_anim_gate_53f0c4` until its render subsystem lands.

## Port plan (incremental, bottom-up + validated per chip)

Each chip is host-tested against the documented post-condition; no mode
flip lands until the world is populated enough for the map arm.

1. **Table-clear phase** — FUN_0046f1dc/2ae/3cd → `engine_game_init_clear_tables`.
   ✅ landed 2026-05-30 (`src/game_init.c`).
2. **Scalar-seed phase** — the 20 `s_slot_`/free-DAT writes in
   `FUN_00497ced`'s body → `engine_game_init_seed_scalars`.
   ✅ landed 2026-05-30 (`src/game_init.c`).  Home decision for the 3
   non-save DATs resolved above (2 relocated to `engine_turn.c`, 1
   parked in `game_init.c`); calendar block recorded as engine-quirk
   #128.
3. **Leaf seeders** — FUN_0042847c, FUN_004ae74d, FUN_0046ff43, the
   FUN_0046f* sibling clearers, etc.  Progress:
   - ✅ **FUN_0042847c** → `engine_game_init_seed_value_table`
     (`src/game_init.c`, landed 2026-05-30).  Resets the 16-record
     value table at save entry 82 (`s_slot_005532a0`) from the retail
     `DAT_004d8910` const table; dest already homed.
   - ✅ **FUN_0046ff43** → `engine_strat_cost_rebuild`
     (`src/engine_strat_cost.c`, landed 2026-05-30).  Strategic
     movement-cost grid builder — see below; lives in its own module.
   - ✅ **FUN_004ac904** → `engine_game_init_seed_unit_table`
     (`src/game_init.c`, landed 2026-05-30).  Config-driven unit/
     opponent-type table seeder — see below; clears the block then fills
     `count` rows from six retail .rdata columns.
   - ✅ **FUN_004ae74d** → `engine_game_init_clear_castle_shadow`
     (`src/game_init.c`, landed 2026-05-30).  Earlier deferred over an
     aliasing/overrun worry; r2 re-analysis (0x4ae791) **resolved it**.
     The producer's base pointer is `0x56d8b8` with a **1-based** column
     (col 1..16, row 0..399; row stride 0x80, record stride 8), so its
     first write lands at `0x56d8b8 + 1*8 = 0x56d8c0` — exactly
     `g_castles_shadow` (save entry 10, already homed, 6400 × 8 B) — and
     its last at entry10+51192, fully in-bounds (no tail overrun; the
     prior note miscounted).  Crucially, starting at col 1 means the 8
     bytes *before* entry 10 are never touched: those are precisely the
     two aliased pre-entry globals the deferral worried about — the
     scalar cursor at `0x56d8b8` (`=10`/wrap-63 in 0x46a295) and the
     freeable pointer at `0x56d8bc` (freed in 0x49915e / set 0x4990f9).
     The function deliberately skips them.  Per 8-B record it zeroes the
     +0 dword and the +4 byte, leaving +5..+7 (the per-castle-slot
     back-pointer lives at +5 — see save-format.md).  Then three world
     scalars (entries 66/67/68) are zeroed.  See below.
   - ✅ **FUN_0046c5b8 + FUN_0046df51** → `engine_territory_clear_cell_field`
     / `engine_territory_clear_cell_render_flags` (`src/engine_territory.c`,
     landed 2026-05-30).  The first two leaf callees of **FUN_004676e0**
     (the overworld-grid setup sub-orchestrator, itself a 0x497ced callee).
     Both bulk-clear the 64×64×8 overworld cell grid — see below.
   - ✅ **FUN_0046da4b** → `engine_territory_rebuild_cell_roster`
     (`src/engine_territory.c`, landed 2026-05-31).  The third
     FUN_004676e0 leaf — rebuilds the per-key cell roster (save entry 12)
     from the grid, capping 20 cells/key + evicting overflow.  See below.

### FUN_004676e0 — overworld-grid setup sequence ✅ ported

> **Orchestrator landed (2026-05-31):** `engine_territory_setup_overworld`
> (`engine_territory.c`) sequences the eleven callees in retail's exact
> order (r2 @0x4676e0): six `clear_cell_field` passes (1,2,4,0,5,3 — wipe
> cell bytes 0..5, leaving +6/+7), `load_strat_map`, `clear_cell_render_
> flags`, `classify_feature_tiles`, `randomise_special_tiles`, **[0x46a037
> deferred]**, `build_army_adjacency`, `setup_capitals`, `seed_army_
> features`, `rebuild_cell_roster(1)`, `refresh_digest`.  It owns the
> persistent single-LFSR `rng` (retail `DAT_00591564`, seeded 0x34518632 at
> InitInstance) and threads it through the two grid-randomiser passes — the
> moment the [engine.h note](../../src/engine.h) anticipated ("the live
> 0x4676e0 then owns the seeded state").  Each pass is NULL-safe on its own
> tables, so a partially-bound world runs only the passes whose inputs are
> present.  3 host tests (end-to-end clear+load+count+digest, rng threading
> on/off, all-unbound no-op).  The one deferred callee, **0x46a037** (the
> per-country bind that loads the in-game country map grid `0x52aff0` + the
> 128×128 preview `0x569590` from map01.pl8 via the IO open-probe
> `0x4af767`), is pure file IO with **no ported consumer yet** — the in-game
> strategic-map arm that reads `0x52aff0` is unported, and the preview is
> filled by the separate page-7 reload path — so it (and `0x4af767`) stay
> deferred until the strategic-map arm forces them.

A 144-B 0x497ced callee that stands up the overworld cell grid + its
terrain index tables, by calling a fixed sequence of 0x46c*/0x46d* grid
helpers (`FUN_0046c5b8`×6, `FUN_0046df51`, `FUN_0046dbf5`, `FUN_0046dd34`,
`FUN_0046da4b`, `FUN_00467ca6`, `FUN_00468d4f`, `FUN_00467a36`), the
strategic-map loader `FUN_00467770`, the digest rebuild `FUN_004291b3`,
and the country-data bind `FUN_0046a037`.

> **Provenance correction (2026-05-31):** earlier revisions of this note
> listed `FUN_00467770` *and* `FUN_0046a037` as "already-ported".  Neither
> was: both tokens only appeared in a deferred-bind **comment** in
> `engine_title_render.c` (`FUN_004337b4`'s per-country reload), which the
> ledger's FUN_-token scan counted as touched — the same false-positive
> the [ledger-provenance audit](ledger-provenance-audit.md) guards against.
> `FUN_00467770` is genuinely ported as of this chip (below);
> `FUN_0046a037` (202 B, loads the per-country 0x4000 blocks via the IO
> open-probe `FUN_004af767`) is the **one remaining** callee.

Callees landed across recent chips:

> **Shared dep unblocked (2026-05-31):** two of the still-unported
> callees, `FUN_0046dd34` and `FUN_0046dbf5` (the new-game grid
> *randomiser* passes that fill the 129×65 terrain-index table
> `DAT_0055cea0` with rolled values), both call the engine's
> single-LFSR RNG `FUN_00404b2c`.  That RNG is now ported as
> `engine_rng_step_single` (a third LFSR alongside the double-LFSR
> `engine_rng_step` — same polynomial, own state `DAT_00591564`, seed
> `0x34518632`, single 7-bit output `DAT_005c9a84`).  See
> `engine_rng.c`.  The two seeders still carry their own grid-mutation
> deps (`FUN_0046de05` / `FUN_0046c2de` / `FUN_0046c080`) before they
> can land, but the shared RNG leaf is no longer in their way.

- **FUN_0046c5b8** → `engine_territory_clear_cell_field(field)`.  Zeroes
  byte `field` of every overworld cell (all 4096).  Shared (also called
  by the reset path `FUN_0046d3da`); both callers sweep fields 0..5 in
  turn, so cell bytes 0..5 are wiped and +6/+7 preserved.  r2 (0x46c5b8)
  confirms byte stores and that the param is the low-byte field offset;
  retail's 8-way-unrolled sweep drives three scratch globals
  (`DAT_0053e8b8` cursor + engine_path's nearest-result pair
  `DAT_00569570/78` reused as loop counters) — all write-only here, so
  the port uses a plain 4096-cell loop and leaves engine_path's result
  globals alone (same call the box scans make).
- **FUN_0046df51** → `engine_territory_clear_cell_render_flags()`.  Clears
  bit 0x20 of byte +2 of every cell, then calls `FUN_0045240a`
  (`engine_dirty_rect_mark_full_screen`).  Byte +2 is a per-cell
  render-flag byte; bit 0x20 is set by `FUN_0046e067`/`FUN_00469370` and
  read by the strategic draw helpers (0x408438 / 0x4063c1 / 0x4071a0 /
  0x406673 / 0x406bba, all gated on `DAT_0053f264 == 1`) — i.e. a "draw
  this cell via the alternate path" mark, wiped at new-game with a full
  repaint.  The repaint is routed through an optional bound dirty state
  (`engine_territory_set_dirty_state`, NULL-safe for host/headless), the
  same pattern the render modules use.  Homed alongside the placer in
  `engine_territory.c` because both functions mutate the same cell grid
  the placer already binds mutably (`engine_territory_bind_cells`).

7 host tests (4 field-clear: one-byte isolation, 0..5 sweep, last-cell
reach, unbound no-op; 3 render-flag: bit-0x20 isolation, full-screen
mark, unbound no-op).

- **FUN_0046da4b** → `engine_territory_rebuild_cell_roster(reset_score_table)`.
  The third leaf, landed 2026-05-31.  Called near the tail of the
  sequence (with `1`) and from the mid-game recompute `FUN_004976a1`
  (with `0`).  It rebuilds the per-key cell roster `DAT_0053ea00` (= save
  entry 12, `g_misc_1360`, **17 rows × 20 i32**): walk all 4096 cells in
  byte order, and for each "active feature" cell — group-key byte +7 in
  1..16 *and* raw-flag byte +1 bit 0x20 set — append its byte-offset to
  the first free (== 0) column of its key's row, capped at 20.  A cell
  whose key already has 20 members finds no free column and is **evicted**:
  type +0 → 0, +3 → 6, flags +1 → 0, +2 `&= 0xe3` (clears bits 2..4).
  Step 1 first zeroes roster rows **1..16** (not 0, not 17) and — only on
  the `reset_score_table == 1` path — the same rows × first 20 columns of
  the parallel AI score table `DAT_0053fa40` (384-i16 rows; the
  strategic demand/price scorer consumes it, unported).  Both tables are
  bound via new optional setters (`engine_territory_bind_cell_roster` /
  `_bind_cell_score_table`, NULL-safe); homed in `engine_territory.c`
  because it scans + mutates the same cell grid the placer binds.  Note
  the `< 0x12` accept guard admits a key (17) the 17-row table can't hold
  — a latent 1-int OOB the +7 producers' tighter `< 0x11` gate keeps
  unreachable; preserved verbatim and documented as **quirk #132**.
  6 host tests (key grouping in scan order, inactive-cell filter,
  20-cap + eviction field map, rows-1..16 clear isolation, score-clear
  reset gating, unbound no-ops).

- **FUN_0046dd34** → `engine_territory_randomise_special_tiles(rng)`.
  The fourth leaf, landed 2026-05-31 — the first ported consumer of the
  single-LFSR RNG (`engine_rng_step_single`) that the prior chip unblocked.
  Sweeps the 129×65 terrain-index render grid `DAT_0055cea0` (i32, row
  stride 0x104 = 65 i32 contiguous) in row-major order and re-rolls every
  cell holding exactly the special sentinel `0xfff0006` to
  `0xfff0006 + (rng & 0xf)` ∈ {0xfff0006..0xfff0015}, advancing the RNG
  once per match.  r2-verified (the `row*65+col` index via `shl 6`+`add`,
  the signed `>= 0xfff0000` / `== 0xfff0006` guard ladder — outer arm
  redundant given the equality, the `& 0xf` payload mask, the `& 7` write
  to scratch `DAT_00568200`).  The RNG state is **threaded through a
  pointer** (`engine_rng_single_in_t *`), exactly as game_init threads the
  double LFSR through its new-game seeders — so no stateful Win32 RNG
  wrapper exists yet; the live 0x4676e0 orchestrator will own the seeded
  single-LFSR state (`DAT_00591564` = 0x34518632) and thread it through
  here and the sibling seeders in sequence.  Two write-only scratch
  globals dropped: the per-cell progress counter `DAT_0053e974` and the
  cross-function mirror residue `DAT_00568200` (no consumer observes what
  0x46dd34 leaves — its readers run earlier and re-derive it).  Homed in
  `engine_territory.c` (the FUN_004676e0 setup family) with a new optional
  tile-grid binding `engine_territory_bind_terrain_tiles` (NULL-safe; the
  render modules bind the same memory read-only).
  6 host tests (row-major reroll order + RNG threading, pinned retail-seed
  value 0xfff0007, non-sentinel/boundary filter with no draw consumed,
  full-grid value range + per-cell advance count, last-cell reach / stride,
  unbound + NULL-rng no-ops).

- **FUN_0046de94 + FUN_0046de05** → `engine_territory_tile_eligible(col,row)`
  / `engine_territory_compute_neighbour_mask(col,row,mask[8])`
  (`engine_territory.c`, landed 2026-05-31).  The neighbour-adjacency
  machinery the sibling seeder 0x46dbf5 (and the feature classifier 0x46c2de)
  drive.  `FUN_0046de94` is the per-tile eligibility predicate: read the
  terrain-index tile at (col,row) of the 129×65 grid `DAT_0055cea0` and return
  1 iff it is a **real cell** (value < `0xfff0000` = the cell's byte-offset
  into `g_overworld_map`) whose flags byte (+1, `DAT_00522f91`) lacks bit 0x4,
  **or** the un-rolled special sentinel `0xfff0006`; 0 if off-grid
  (col∉[0,65) ∥ row∉[0,129)) or any other special value.  The signed bounds
  ladder (`-1 < col/row`, `< 0x41`/`< 0x81`) is load-bearing — `FUN_0046de05`
  pushes neighbours off the edge for border tiles, and those must read as
  ineligible (r2 @0x46de94 confirmed the `row*0x104 + col*4` index, the
  `< 0xfff0000` real/special split, the `flags & 4` test, and the
  `== 0xfff0006` sentinel arm).  `FUN_0046de05` is the 8-neighbour sweep:
  the strategic map is a **doubled-row hex grid**, so it picks one of two
  offset tables by row parity (`row & 1`) — retail `DAT_004d6c28` (even) /
  `DAT_004d6be8` (odd), each 8×{i32 dx, i32 dy}, dumped + transcribed
  verbatim — and stores the predicate result for each into the shared scratch
  global `DAT_00543fe0[8]` (read by 9 functions, a produce-then-consume
  register never carrying cross-pass state).  The port threads it explicitly
  through the caller's `mask[8]` rather than a mutable global, the same idiom
  the RNG uses.  6 host tests (real-cell flag gate, special-value
  discrimination, off-grid bounds incl. the inclusive (64,128) extreme,
  even-row mask + single-neighbour flip, odd-row parity discriminator,
  corner off-grid clamping).

- **FUN_0046c2de** → `engine_territory_pick_feature(mask[8], *out)` +
  `engine_territory_reset_feature_counters()` (`engine_territory.c`, landed
  2026-05-31).  The hex **auto-tile / feature-variant classifier** — the call
  that consumes the neighbour mask the pair above builds.  It walks retail's
  **48-entry pattern table** `DAT_004d7310` (12 B/entry; dumped + transcribed
  verbatim): `pattern[0..7]` over the 8 hex neighbours (0 = must be
  ineligible, 1 = must be eligible, 2 = wildcard), then `type (+8)`,
  `aux (+9, 0 throughout)`, `cap (+0xa)`, and a per-entry rotating variant
  counter `(+0xb)`.  The first row whose 8-byte pattern matches the mask wins;
  the function emits that row's `type` → `DAT_0057c8a4` and a rotated variant
  → `DAT_00568200` (`++counter`, wrap at `cap`), returning the entry index+1.
  r2 @0x46c2de confirmed the three-way per-byte match (`==2 ∥ (≠0 ∧ mask≠0) ∥
  (==0 ∧ mask==0)`), the `cap <= counter → 0` wrap (a `jg` over the reset),
  and the byte-8/9/0xb global writes.  The table is **complete** — every one
  of the 256 neighbour configs matches some row (entry 46, all-zero pattern,
  is the isolated-tile catch-all with `cap 8`; entry 47, also all-zero, is an
  unreachable terminator), so retail never returns 0 here.  c2de is shared
  by **6 callers** (0x46dbf5 + five 0x47* strategic seeders), each reading the
  type/variant globals right after — so the port threads the result through
  an `engine_territory_feature_pick_t` out-struct, keeping only the rotating
  counters as module state (retail never resets them; the reset helper seats a
  deterministic start for the harness/tests).  6 host tests, all pinned
  against the embedded table (all-eligible→entry 0/0x4e, all-ineligible→entry
  46/0x16, first-match-wins via only-pos0→entry 15, cap-2 rotation 1·0·1·0 +
  cap-8 wrap 1…7·0, cap-1 always-0, and end-to-end from compute_neighbour_mask).

- **FUN_0046dbf5** → `engine_territory_classify_feature_tiles(rng)`
  (`engine_territory.c`, landed 2026-05-31).  The **terrain auto-tiling pass**,
  sibling of the special-tile randomiser `FUN_0046dd34`.  Sweep the 129×65
  terrain-index grid in row-major order; for every **feature-border** cell — a
  real cell (tile < `0xfff0000`) whose flags byte (+1) has bit 0x4 **set**, or
  a special tile that is **not** the sentinel `0xfff0006` — build the
  8-neighbour mask (`compute_neighbour_mask`), classify it (`pick_feature`),
  and write the chosen transition tile: real cells into the descriptor's +3
  byte (`(uint8_t)(type+variant)`), special tiles into the tile itself
  (`0xfff0000 + type + variant`).  Isolated tiles (classifier type `0x16`)
  override the table variant with a fresh single-LFSR roll (`rng & 7`).  Note
  the border test is the *complement* of the neighbour predicate: cells that
  predicate as ineligible are exactly the ones swept.  r2 @0x46dbf5 confirmed
  the dual write paths, the `type==0x16` RNG override, and the `de05(row,col)`
  arg order.  The grid is mutated **in place** in row-major order (rewritten
  special tiles feed later masks; real-cell writes touch only +3 and never
  perturb the grid), so the result is order-dependent and snapshot-free; the
  RNG is threaded through `*rng` like the other seeders.  5 host tests (real
  border → +3 byte, special border → tile rewrite, all-sentinel no-op + no
  draw, the corner-cell type-0x16 RNG override pinned to the first draw,
  unbound/NULL no-ops).

The two FUN_004676e0 grid **randomiser passes** (`FUN_0046dd34` special-tile
re-roll + `FUN_0046dbf5` auto-tile classify) and their full support cluster
(`FUN_0046de94`/`de05` mask, `FUN_0046c2de` classifier, `FUN_00404b2c` RNG)
are now ported.

- **FUN_0046c080 + FUN_00467ca6** → `engine_territory_load_neighbour_keys`
  / `engine_territory_build_army_adjacency` (`engine_territory.c`, landed
  2026-05-31).  The **per-army territory-adjacency builder** and its
  orthogonal-neighbour key loader.  `FUN_0046c080` (199 B, shared — also
  called by the castle-border subtree `FUN_0046902a` under `FUN_00468d4f`)
  reads the owning-army key (cell byte +7) of the **four orthogonal**
  neighbours (N/E/S/W) of the cursor cell, clamping an off-grid edge
  neighbour to 0; retail scatters them to `DAT_00553280/88/90/98`
  (stride-8 slots, only the even ones used), the port returns a compacted
  `out[4]`.  `FUN_00467ca6` (385 B) sweeps all 4096 overworld cells in
  byte order and, for every cell whose key is in 1..17, records each
  *distinct* in-range neighbour key (≠ the cell's own key, ≠ 0) into that
  army's row of the per-army record block (retail `DAT_0053f9b0` = save
  entry 4, `g_per_army_768`, 17 rows × 0x300): a 16-entry list at +0x5c
  with a count at +0x5a.  r2-verified both (the `key*0x300` row index via
  `shl 8`+`lea eax+eax*2`, the +0x5a/+0x5c field offsets, the dedup-then-
  first-free insert with the `< 0x10` cap, the four neighbour deltas
  ±0x200/±8 and their edge guards).  The score table the rebuild zeroes
  (`g_cell_score` = `DAT_0053fa40`) is the **same block** at +0x90 — a
  different field of the same rows.  The 16-cap silent-drop arm is dead in
  valid data: with keys gated 1..16 an army borders at most 15 distinct
  others (key 17 can't be a swept centre — see below), so the list never
  fills.  **Quirk #132** extended: 467ca6 shares the rebuild's loose
  `<= 0x11` accept guard against the 17-row block (a key-17 centre is a
  1-row OOB write), defused identically by the +7 producers' 1..16 clamp.
  6 host tests (N/E/S/W interior order, corner edge-zeros both extremes,
  distinct-neighbour record + symmetry, cross-cell dedup, 15-key near-cap
  fill + scan order, unbound/NULL no-ops).

- **FUN_0046d9da** → `engine_territory_clear_cell_score` (`engine_territory.c`,
  landed 2026-05-31).  A 113-B **leaf** at the bottom of the `FUN_00467a36`
  subtree (`467a36` → `FUN_0046d7f4` → this).  Given a cell byte-offset, it
  scans that cell's key row (key = its +7 byte, `DAT_00522f97`) of the per-key
  roster `DAT_0053ea00` for the column holding the offset, and zeroes the
  parallel i16 in the AI score table `DAT_0053fa40` (`key*0x300 + col*2`) —
  the same two tables the roster rebuild (`FUN_0046da4b`) owns.  It is the
  per-cell score-invalidation the cell-mutation helper `FUN_0046d7f4` fires
  when a cell's feature changes (the rebuild does the bulk reset; this clears
  one slot).  Only the score is touched; the roster entry stays.  A cell
  absent from its row (all 20 columns miss) clears nothing — retail falls
  through the loop.  r2 @0x46d9da confirmed the `key*0x50 + col*4` roster
  index (`lea ecx,[ecx+ecx*4]; shl 4`), the equality on the offset arg, the
  `key*0x300 + col*2` score write, and the col 0..19 (`< 0x14`) bound.  No
  new quirk: unlike its rebuild sibling, the key here is a memory read with
  no `< 0x12` accept guard, but it indexes the same 17-row tables and the +7
  producers' 1..16 clamp keeps it in-bounds.  4 host tests (matched-slot
  isolation, absent-cell no-op, cell's-own-key-row selection vs a same-offset
  decoy in another row, unbound roster/score no-ops).

- **FUN_0046d7f4** → `engine_territory_set_cell_type` (`engine_territory.c`,
  landed 2026-05-31).  The engine's **overworld cell terrain-type mutator** —
  the mid-tier of the `FUN_00467a36` subtree and the sole caller of the leaf
  above.  Given `(cell_off, type, variant)` it writes the type byte (+0),
  then per type derives a +3 render-descriptor base and the +2 flag-bit
  contribution: type 0 → base 0x50 (+ clears the cell's AI score via
  `FUN_0046d9da`); 0x17/0x18 → 0x82/0x86 with **no** +2 low bits; 1 → 0x54;
  0x19..0x1c → 0x6c/0x70/0x74/0x78; type < 0x13 → 0x58; else 0x68.  The +3
  byte keeps its low 2 bits (corrected for an old 0x17/0x18 special) and
  rebuilds the rest as `base + variant*4`.  The +2 byte gets bit0 set, bits
  2..4 replaced (bit3 for ordinary types, nothing for the specials), bit7
  cleared then re-set only for the type band 15..22 (`0xe < type < 0x17`).
  r2 @0x46d7f4 caught a **Ghidra error**: the decompiler renders the +3
  low-bit correction constant as 0 (default) / 0x82, but the real default is
  **0x50** (`mov byte [var_ch], 0x50` @0x46d8e6) — it survives only through a
  `& 3`, and 0x50 ≡ 0 (mod 4), so the two are equivalent (documented in the
  port comment).  6 host tests (ordinary type's base+flags + no score clear,
  type-0 base 0x50 + score clear + low-bit preserve, band bit7, special
  no-low-bits + no bit7, old-special correction + variant tier, unbound
  no-op).

With `FUN_0046d7f4` ported, `FUN_00467a36`'s subtree is **leaf-complete**
(`467a36` → `46d7f4` → `46d9da`, both below it now landed).

- **FUN_00467a36** → `engine_territory_seed_army_features` (`engine_territory.c`,
  landed 2026-05-31).  The new-game **per-army feature-seeding pass** — the
  7th of `FUN_004676e0`'s eight direct callees.  For every active feature-cell
  (key +7 in 1..17, flags +1 bit 0x20) it assigns a feature terrain type by
  the army's *running* active-cell index and the global feature-density
  setting `DAT_0053f23c` (= `s_slot_0053f23c`, save entry 13): the first few
  cells of each army get the rich type `0x14` (descriptor base `'h'`), then
  type `1` (`'T'`), then type `0` (`'P'`), per-density cutoffs —
  density 3: `<4→0x14, else 0`; 2: `<4→0x14, <6→1, else 0`; 1: `<6→0x14,
  <8→1, else 0`; default(0): `<8→0x14, else 1`.  Each cell is finalised by
  pre-writing +3/+0 then calling the ported `engine_territory_set_cell_type`
  (`FUN_0046d7f4`); the **+0 pre-write is load-bearing** — it makes the setter
  read the *new* type as its "old type", so the setter's 0x17/0x18 +3
  correction never fires here (new type is always 0x14/1/0).  A per-army
  active-cell tally is kept at row `+0x205`.  r2 @0x467a36 confirmed the
  `key<<8`+`lea[eax+eax*2]` = `key*0x300` row index, the `+0x205` tally byte,
  the `(desc3+0xb0)&3` low-bit preserve, the three `DAT_0053f23c` compares,
  and the `idx<4/6/8` cutoffs.  **Quirk #132** extended a third time (the
  `< 0x12` key guard vs the 17-row block at the `+0x205` tally).  6 host
  tests (density-2 tiers + setter-derived +3/+2 bytes, density-1 & density-3
  cutoffs, default-density arm, inactive-cell filtering, per-key-independent
  indices + step-1 tally reset, unbound no-ops).
- **FUN_00467770** → `engine_territory_load_strat_map` (`engine_territory.c`,
  landed 2026-05-31).  The **strategic-map record → cell-grid unpacker** and
  the first `FUN_004676e0` callee (it reads the world before the grid passes
  refine it).  Retail reads the selected 0x80C1-byte record from
  `l2_maps.dat` (`io_read_file_at`, offset `map_index*0x80C1`) into the
  shared asset scratch buffer `DAT_004eabec`, then walks all 64×64 cells:
  the record's first six 0x1000-byte cell-linear planes map to cell bytes
  +1,+2,+3,+4 (planes 0–3) and +7 (plane 5); the **player/army count**
  `s_slot_0056d5dc` is recomputed as the max plane-5 value in the 0..0x10
  player band (cells with plane5 > 0x10 are non-player markers); and where
  **plane 4 is non-zero** the cell seeds either a category bucket (plane-0
  bit 0x40, via `FUN_00429153`) or the player roster (bit 0x80, via
  `FUN_0049bbe8`) — **0x40 wins** when both bits are set.  Up front it resets
  the render rotation offset `DAT_00522f7c`, zeroes both counts, and clears
  the bucket table + roster (`FUN_004290d0` / `FUN_0049bc1d`).  r2 @0x467770
  confirmed the plane strides, the `plane5 <= 0x10 && count < plane5` count
  rule, the cell-byte targets (`522f91/92/93/94/97` at stride 8), and the
  cdecl arg order of both seed calls.  The **file read is the deferred live
  Win32 seam** (engine modules never touch the filesystem — io.h): the host
  binds a synthetic record, so the index-independent unpack is fully tested
  (5 host tests: plane→cell copy, in-band-max count, bucket+roster seeding +
  the 0x40-wins tie-break, up-front clears, unbound/NULL-bucket no-ops).
  `DAT_004eabec` is the engine's **shared ~200 KB asset scratch buffer**
  (also loaded with vill.pl8/smithy.pl8/iconvill.pl8/… elsewhere, and bound
  by `engine_mp_render` as a "menu sprite asset" — same reused buffer, not a
  distinct global).  `DAT_0053e974`, the running cell offset, is a scratch
  loop-counter global reused by several loaders (reset to 0 on entry, never
  read across calls) → modelled as a local.

### FUN_00468d4f — per-army capital-region setup (subtree complete)

The **last** unported `FUN_004676e0` callee (274 B): a loop over armies
`1..DAT_0056d5dc` (= `s_slot_0056d5dc`, save entry 77, the player/army
count) that, for each army, stands up its home region on the overworld
cell grid and stashes the result into the per-army record row
(`g_army_records`, retail base `DAT_0053f9b0` + the army's `0x300` row):

```
cap = FUN_00467fd1(k);   row[+0x70] = cap;   // capital cell offset
if (cap != 0) {
    row[+0x6c] = col;  row[+0x6d] = row;     // stash cursor around 0x468c41
    FUN_00468c41(k);                         // collect bordering cells
    col = row[+0x6c]; row = row[+0x6d];       // restore cursor
    FUN_00468e61(k);                         // mark special feature cells
    FUN_0046902a(k);                         // place coastal port (nearest to cursor)
    row[+0x78] = FUN_00468121(k);            // secondary cluster
    row[+0x74] = col;  row[+0x75] = row;     // stash secondary cursor
}
```

The cursor col/row are retail scratch globals `DAT_0057c8d0` / `DAT_0056846c`
(modelled as `engine_territory.c` module statics `g_cursor_col` / `g_cursor_row`,
read back into the army record and fed to the port placer as a distance
origin).  Subtree port progress (all in `engine_territory.c`, the shared
cell-grid module):

- ✅ **FUN_0046ac22** → `engine_territory_stamp_block` (2026-05-31).  Stamps a
  `size`×`size` descriptor-gradient block onto the grid: per cell `+2 |= 1`,
  clear `+2` bits 2..4, `+3 ← base + fill[k]`, `+2 |= flag`, `+0 ← type`.  The
  N×N fill-order tables are retail `DAT_004d80e0`/`e8`/`f8` (r2-verified;
  2×2 = `{0,2,1,3}`).  Every reachable call site passes size 2, so the 3×3 /
  4×4 tables are transcribed-but-dead.  Shared (also the AI/event castle
  placers at 0x4685../0x46902a call it), hence homed here not in game_init.
- ✅ **FUN_00467fd1** → `engine_territory_place_capital` (2026-05-31).  Row-major
  scan for army `key`'s capital-eligible cells (`+7 == key`, `+1 bit 0x40`,
  `+0 == 0`); 1st match gets the 2×2 capital stamp + `+2 bit 0x80` and is the
  return value, the 3rd also gets `+2 bit 0x80`, the 4th seats the cursor
  col/row.  Fewer than 4 eligible cells → returns 0 (cursor untouched).
- ✅ **FUN_00468c41** → `engine_territory_collect_keyed_cells` (2026-05-31).
  Collect the army's claimed cells (`+1 bit 0x10`, `+7 == key`) into the
  record's `+0x80` i32 cell-list (uncapped, terminator-free — relies on the
  pre-zeroed region; cell-(0,0) blind spot), copying `+3 → +6` and clearing
  `+0`.
- ✅ **FUN_00468e61** → `engine_territory_mark_capital_features` (2026-05-31).
  For in-capital cells (`+7 == key`, `+2 bits 2..4 == 0xc`), route by the `+3`
  descriptor into one of three per-army feature slots (`0x1e → +0x2ad`,
  `0 → +0x2dd` gated on slot-A unset, `0x14 → +0x295`), stamping `+0` type
  (1/4/0x0a) and `+2 bit 0x80`.
- ✅ **FUN_0046c147** → `engine_territory_classify_border_neighbour` (2026-05-31).
  Classify an N/E/S/W neighbour for the port placer: `(nbr +1 & 0xfd)` with
  an `8↔0x10` remap keyed on the neighbour's `+2` bits; 0 off-grid.  Retail's
  dead edge read of the neighbour's `+2` dropped → no OOB.
- ✅ **FUN_0046902a** → `engine_territory_place_capital_port` (2026-05-31).
  Among empty key cells bordering a same-key coast neighbour, stamp a port
  (`+0 ← 7`, `+3 ← 0x0a`, `+1 bit 0x80`, `+2 = …&0xe3|0xc|0x80`) at the one
  closest (Chebyshev) to the capital cursor; record at `+0x2c5/+0x2c8`.
  Chebyshev = `FUN_00404f4c` inlined, dx/dy globals dropped (no consumer).
- ✅ **FUN_00468121** → `engine_territory_place_secondary` (2026-05-31).  First
  four cells (`+7 == key`, `+1 bit 0x80`, `+0 == 0`) get `+6 ← +3` and
  `+0 ← 0x14`; 1st also `+2 bit 0x80` and seats the cursor on **itself**
  (vs the capital finder's 4th cell — see quirk #134).
- ✅ **FUN_00468d4f** → `engine_territory_setup_capitals` (2026-05-31).  The
  orchestrator above; loops `1..s_slot_0056d5dc` and wires the six passes,
  stashing capital/secondary offsets + cursor col/rows into `+0x6c..+0x78`.
  Retail's cursor save/restore around the collect pass is a vestigial
  identity round-trip (quirk #134) — the port keeps the stash, drops the
  read-back.

**With this, `FUN_004676e0`'s 11 direct callees are all ported** — the
overworld-grid setup orchestrator is now itself leaf-complete and is the
next chip (assemble `FUN_004676e0`, then its caller `FUN_00497ced`).

### FUN_0046ff43 — strategic movement-cost grid builder

A 617-B terrain classifier (a callee of 0x497ced) that fills the 64×64
**strategic** movement-cost grid `DAT_004f4080` (4096 cells × i16) from
the overworld terrain descriptors `g_overworld_map` (`DAT_00522f90`,
save entry 0 — 8 B per cell).  Per cell it reads byte +0 (terrain type),
byte +1 (flags), byte +7 (a level/count, gated `< 0x11`) and maps them
to a cost word via a fixed branch ladder (flags 0xc → impassable 0;
bit0 road → 1; bit5 → 3/6/3 by type; bit6 → 100; bit7 → mostly 0/100;
bit4 → 100/0; else 3).

This is a **distinct pathfinder from `engine_path`** (the 80×80 army
router on the cost map at 0x504030).  Its grid is consumed by the
still-unported strategic flood-fill `FUN_0046f700` and path-trace
`FUN_004701ac`, which read it at directional neighbour offsets
(±2 = ±1 col, ±0x80 = ±1 row).  Homed in its own module
(`engine_strat_cost.c`) so those ports can share the grid when they
land.
4. **Heavy seeders** — surveyed one at a time.  Progress:
   - ✅ **FUN_00497e65** → `engine_game_init_seat_shuffle`
     (`src/game_init.c`, landed 2026-05-30).  New-game randomiser — see
     below.
   - ✅ **FUN_0049aa0e** → `engine_ranking_recompute`
     (`src/engine_ranking.c`, landed 2026-05-30).  Player standings /
     scoring / victory recompute — its own module (5 callers, not
     init-only).  See below.
   - remaining: FUN_0049bd99 (3 KB, largest), FUN_004984dc, FUN_00451150,
     FUN_00448440 (each still has 5–28 unported callees — not yet leaves).

### FUN_004ac904 — unit/opponent-type table seeder (config-driven)

A 295-B leaf seeder (sole caller 0x497ced) that fills the unit/opponent-
type table `g_unit_table_20` (save entry 6, 13 records × 0x14 B, retail
0x568dc0) with a **scenario-selected number of rows** drawn from six
parallel retail `.rdata` columns.

- **Row count.**  The config/scenario selector `s_slot_0056d5dc` (save
  entry 77, a heavily-read global) indexes a 20-entry lookup table
  (`DAT_004de820`, values ramping `0,1,2,2,3,4,…,12` then flat at 12;
  bounded in `.rdata` by the `"ff_batl.wav"` string at 0x4de870).  The
  looked-up count is clamped (`>12 → 1`, `<0 → 1`) and **persisted to
  `s_slot_00554030`** (save entry 122 — this function is its *sole*
  writer).  The clamp is **dead for any valid selector 0..19** (the table
  never exceeds 12 and is non-negative); it is retail's defence against
  an out-of-range selector reading past the table.  Note a table value of
  **0** (selector 0) is *not* clamped to 1 — it persists as 0 and the
  1-based fill loop runs zero times.

- **Clear-then-fill.**  Re-issues the unit-table clearer `FUN_0046f2ae`
  (the same one the table-clear phase runs), then for record `i` in
  `1..count` copies eight fields from six columns:

  | rec off | src column (`.rdata`) | type | note |
  |--------:|-----------------------|------|------|
  | +0x2    | `DAT_004de79f` (col A) | byte | also written to +0x4 |
  | +0x4    | `DAT_004de79f` (col A) | byte | dup of +0x2 |
  | +0x5    | `DAT_004de78f` (col B) | byte | |
  | +0x6    | `DAT_004de7af` (col C) | byte | also written to +0x7 |
  | +0x7    | `DAT_004de7af` (col C) | byte | dup of +0x6 |
  | +0x8    | `DAT_004de75c` (col D) | dword | |
  | +0xc    | `DAT_004de7bc` (col E) | dword | |
  | +0x10   | `DAT_004de7ec` (col F) | dword | |

  Bytes +0x0/+0x1/+0x3 are left cleared.  Record 0 stays cleared (loop is
  1-based).

- **`.rdata` packing quirk.**  The six columns are laid out so each
  column's entry[0] *aliases the previous column's entry[12]* — e.g.
  col_e[12] and col_f[0] are the literal same dword at 0x4de7ec.  Since
  only entries `1..count` are read, the overlap is harmless; the port
  stores the real entry-0 bytes for a faithful copy.

- **Field semantics are provisional.**  The 12 meaningful rows pair up
  by col B (`4,4,5,5,3,3,1,1,2,2,6,6` → six 2-variant classes) and col A
  (`1,2 / 4,5 / 7,8 / …`); cols D/E/F are stat dwords (D∈50..250,
  E∈1800..6000, F∈180..600).  Consumers index `g_unit_table_20` through a
  computed base pointer (no static field xrefs), so the exact meaning is
  left for when a reader subsystem lands.  Ported byte-exact regardless.

Homed in `game_init.c` (a 0x497ced callee); the six columns are embedded
as `static const` arrays mirroring the retail `.rdata`, extracted
byte-for-byte from the PE.

### FUN_004ae74d — clear castle-shadow table (+ 3 world scalars)

A 144-B init-only callee (sole caller 0x497ced) and a direct
orchestrator leaf.  It resets the **castle-shadow** table —
`g_castles_shadow`, save entry 10 (retail `0x56d8c0`), 6400 records of
8 B, a parallel of the live castle table (entry 1).  r2 (0x4ae78b) shows
a 400×16 double loop: the dword write is `[row*0x80 + col*8 + 0x56d8b8]`
and the byte write `[row*0x80 + col*8 + 0x56d8bc]`, with **row 0..399**
and **col 1..16**.  Because the base is `0x56d8b8` (= entry10 − 8) and
col is 1-based, the writes cover entry10 offsets `{row*128 + (col-1)*8}`
= every 8-B record start from 0 to 51192 — all 6400 records, exactly
in-bounds, no overrun.  Per record it zeroes the +0 dword and the +4
byte; +5..+7 are preserved (the per-castle-slot back-pointer is at +5,
per save-format.md).

The col-1 start is not incidental: it makes the loop **skip** the 8
bytes immediately before entry 10 (`0x56d8b8`/`0x56d8bc`), which are two
unrelated runtime globals aliased into that gap — a scalar cursor
(`0x56d8b8`) and a freeable pointer (`0x56d8bc`).  This is why an earlier
pass deferred the chip; the resolved reading is that those globals are
untouched by this function and belong to other subsystems.

After the table sweep it zeroes three world scalars: `s_slot_0055300c`
(entry 66), `s_slot_00568da8` (entry 67), `s_slot_00553f30` (entry 68) —
all already homed.  Ported byte-exact to `game_init.c`; two host tests
pin the per-record clear/preserve split (every record's +0..+4 zeroed,
+5..+7 kept) and the scalar reset.

### FUN_00427ed0 — army start-marker placement (+ entity placer FUN_0046e1b0)

A 311-B init-only callee (one of three callers, but the new-game path is
0x497ced) that drops each starting army's marker onto the strategic map.
It walks the army-id list at **save entry 139** (`s_slot_00569518`, up to
6 ids, terminated by a zero), and for each id:

1. Probes the map for a nearby feature via the two army-coord predicates
   (`FUN_00428007` bit0, then `FUN_00428078` flag∈{0,2}, both within
   Chebyshev radius 1..3 of the army's coord — the box-scan callers from
   the prior chip).  The probes now return the **matched cell** through
   out-params (resolving the quirk #130 trap).
2. On a hit, allocates a **type-6 entity** (kind 3) at that cell via the
   shared placer **FUN_0046e1b0**, then stamps army-specific fields into
   the freshly allocated record (addressed by the placer's current-slot
   cursor, retail `DAT_0052afe0`): `+0x150 = 1`, `+0x10 = id`,
   `+0x166 = 100`, `+0x167 = id`, `+0x164 = 1`, `+0x14f = loop index`.
3. Bumps the placed-army counter at **save entry 78** (`s_slot_005530b4`).

**FUN_0046e1b0** (792 B, *not* init-only — also reached by the
0x4a0015 / 0x4a551b / 0x4ac185 setup paths) is the engine's map-entity
allocator over the 151×420 record table (`g_per_territory_420`, retail
`DAT_0052f0b0`).  It validates (col,row), requires the target cell free
(head-link +5 == 0 and raw-flag +1 & 0xfc == 0), scans records 1..149 for
the first free slot (type byte +0 == 0), zeroes it (`FUN_0046ead0`,
memset 0x1a4), writes the position/type field map (see
`engine_territory.c`), and claims the map cell (cell +5 ← slot, +2 |= 1).
Because it is shared sim, it lives in `engine_territory.c`
(`engine_territory_place_entity`), not `game_init.c`; FUN_00427ed0 itself
is homed in `game_init.c` as `engine_game_init_place_army_starts`.  10
host tests (7 placer + 3 army-start loop incl. the matched-cell report).

> The placer needs the map-cell grid bound mutably
> (`engine_territory_bind_cells`) in addition to the record array; the
> live orchestrator (game-start step 5) wires both when assembled.

### FUN_004a1c53 — reset per-player sub-tables (+ FUN_004a2584 grid clear)

A 537-B init-only callee (sole caller 0x497ced) that resets the
per-player sub-tables ahead of the heavy seeders.  Three steps: zero
`s_slot_0057a0f8` (entry 144); selectively clear the entry-146 6×5 grid
of 8-B elements (`FUN_004a2584`, a static helper — zeroes each element's
+0/+1/+2 and the dword +4, leaving byte +3); recompute the
first-active-non-local-player cursor (`FUN_004a1e6c`, the shared roster
helper landed in chip 7).  Then, for each player slot 1..5 of
`g_player_strategic_352` (stride 0x160; slot 0 skipped), it resets a
**5-entry sub-table** of 0x10-B elements at record offset 0x94 (sub =
1..5) plus two standalone bytes (+0x81, +0xeb).  Each element's lead byte
(+0x0) is seeded to **5 for an AI player** (`F_ACTIVE` set AND
`F_HAS_DATA` clear) and 0 for human/inactive; +0xd is set to 1; the other
written fields are zeroed; element bytes +0x6/+0x7 are left untouched.
r2-confirmed write set (0x4a1d32…0x4a1e3a); field semantics provisional
(consumers reach the sub-table through a computed pointer).  Homed in
`game_init.c`.

### FUN_0049b35e — purge inactive players' strategic records

A 205-B init-only callee (sole caller 0x497ced) that scrubs leftover /
garbage player state before the heavy seeders run.  Walks player slots
1..5 of `g_player_strategic_352` (save entry 3, stride 0x160; slot 0 —
the reserved front slot — is skipped) and zeroes the whole 0x160-byte
record (via `FUN_0046ea61`, the len-0x160 instance of the fixed-length
zeroer — a fourth sibling of FUN_0046ea28/eb09/ead0) when the slot is
either **unoccupied** (`+0x4 == 0`) or **occupied-but-uninitialised**
(`+0x5 == 0 AND +0x7 == 0`).  The three fields are the same
`F_ACTIVE` / `F_HAS_DATA` / `F_OPP_TYPE` offsets `engine_ranking.c`
names; the half-init exemption needs *both* the human-data flag and the
AI opponent-type code clear, so a record with either set survives.
Homed in `game_init.c`.

### FUN_00497e65 — new-game randomiser (RNG seats + two 48-tables)

A 1025-B seeder (sole caller 0x497ced) that consumes one contiguous run
of the double-LFSR RNG (`FUN_00404a46`, ported as `engine_rng_step`;
each draw reads `DAT_0058fd60` = the 7-bit-b mirror) and produces three
randomised pieces of new-game state, in a fixed order whose draw count
is part of the persisted result:

1. **Roster shuffle.**  Re-seats the `active` occupied roster pairs
   (`g_player_roster`, seats 1..5; seat 0 — the reserved front slot — is
   excluded from both the count and the shuffle) into random array
   positions, carrying each pair's `{army-id, seat-id}` payload together.
   Assumes the roster has been compacted (`player_roster_compact` runs
   earlier in setup) so the occupied pairs are contiguous at 1..active.
   Snapshots all 6 pairs, clears only the id bytes, then forward-probes
   (wrap 1..active) for an empty slot per pair.  Consumes `active` draws
   (`& 3` target bias).

2. **48-entry permutation table** (save entry 178, `s_slot_0057cae0`):
   identity values 0..47 each dropped into a random empty slot (never
   its own index), forward-probing with a 0x32-attempt cap and 0x30 wrap
   (`& 0x1f` bias).  48 draws.  In-game it is indexed by the shared
   cursor `DAT_005653f8` → `DAT_00553528` (readers 0x47aaa3 / 0x444a2f).

3. **48-entry value/threshold table** (save entry 191, `s_slot_00553080`):
   each cell a categorical roll `(rng & 7)` → 0 (roll 5), 100 (roll 6),
   else `roll·5+6` ∈ {6,11,16,21,26,41}.  48 draws.  Read as an AI
   threshold (compared against `DAT_00553f28` at 0x48acd2 / 0x48b02b,
   same cursor).

Homed in `game_init.c` (a 0x497ced callee).  Ported verbatim, r2-verified
(masks 3 / 0x1f / 7, the `+1` target bias, the 0x30 wrap, the 0x32 probe
cap, the value ladder `eax·5+6`).  The RNG is threaded in/out via an
`engine_rng_in_t *` so the live orchestrator (step 5) can continue the
draw sequence from the prior seeders; tests pass a fixed seed.

### FUN_0049aa0e — player standings / ranking recompute

A 2197-B routine (5 callers: new-game 0x497ced, plus the turn/event
paths 0x49b42b / 0x49cebe / 0x49a010 / 0x435211) that recomputes the
whole-game standings from the 6 per-player strategic records
(`g_player_strategic_352`, stride 0x160).  Because it is **not
init-only**, it lives in its own module `engine_ranking.c` rather than
`game_init.c`.  One pass, six phases:

- **A** clears 19 leaderboard result scalars (save entries 124, 151–168).
- **B** finds, for each of 8 strategic metrics, the max value + holding
  slot, plus an active-player count that excludes the local player.
- **C** writes each active player's composite score to record +0x50: a
  weighted sum (`+0x60·10`, `+0x10÷10`, `+0x0c·2`, `+0x58·2`, `+0x54÷5`,
  `+0x4c·50`) plus a band on the i32 metric +0x118.  **The band is dead
  past the first arm** — the `< 2001` guard makes the `+100` / `+200`
  arms unreachable, so only `>= 2001 → +50` ever fires.  r2-confirmed at
  0x49aed1; ported verbatim including the dead arms.
- **D** builds a 5-entry `{slot, score}` board (`s_slot_00565410`, entry
  150, 6×8 with a trailing zero sentinel) and bubble-sorts it descending.
- **E** stamps the 1-based finishing rank into record +0x2b; inactive
  slots get rank 0 and drop out.
- **F** records winner (first ranked, entry 151) and last (entry 152).
- **G** when only one ranked player survives (winner == last), latches
  the per-player victory flag (+0xed) and posts a victory / notification
  event via the ported producer `engine_event_queue_post` (FUN_00472bc5)
  — the AI arm (code 0xc3, payload from the rotating +0x159 counter and
  the opponent-type +0x07) or the human/already-latched arm (code 0xe1,
  targeted at the local player).  This is the function's only side
  effect outside the save globals, so the event-queue state is threaded
  in as a pointer; the `player_has_data[7]` filter is rebuilt from the
  table (slot 6 — which would overrun the 6-slot array — stays 0, and is
  never read here since `target ∈ 1..5`).

At new-game with multiple players, winner != last, so phase G does not
fire; it fires later when the field narrows to one survivor.

### FUN_00469b8d — per-army terrain census (first leaf of FUN_0049bd99) ✅ ported

The first per-army callee inside the still-unported army/country setup pass
`FUN_0049bd99` (3 KB, the largest remaining init callee).  `FUN_0049bd99`
loops the realm keys 1..realm-count, seeds each realm's `g_per_army_768`
record, and calls `FUN_00469b8d(key)` to **census the realm's terrain**.

The census walks the realm's per-key cell roster (`g_cell_roster` =
`DAT_0053ea00` / save entry 12, 20 i32 per row — the same table
`engine_territory_rebuild_cell_roster` fills) and, for each non-zero column
(a cell byte-offset), reads that cell's +0 TYPE byte from the overworld grid
(`g_cells` = `DAT_00522f90`) and bumps one of five counters in the realm's
record (`g_per_army_768` row + 0x1ff..0x204):

| record byte | retail DAT | counts TYPE |
|-------------|-----------|-------------|
| +0x1ff | 0x53fbaf | `== 1` |
| +0x200 | 0x53fbb0 | `0x0f..0x16` |
| +0x201 | 0x53fbb1 | `0x02..0x0e` |
| +0x203 | 0x53fbb3 | `0 / 0x17 / 0x18` |
| +0x204 | 0x53fbb4 | `>= 0x19` |

`+0x202` (`DAT_0053fbb2`) sits inside the run but is **not** a bucket and is
never touched — a separate field.  All three tables are already-externed
shared save blocks (no new globals), so the port slots straight into
`engine_territory.c` reusing its existing `g_cells` / `g_cell_roster` /
`g_army_records` binds.  Verified against r2 @0x469b8d (the type ladder and
the `key*0x300` / `key*0x50` strides).  See engine-quirks #135 for the
daisy-chain counter init.  Ported as `engine_territory_census_army_terrain`;
5 host tests (bucket split, band boundaries 1/2/0xe/0xf/0x16/0x17/0x18/0x19,
empty-slot + other-row isolation, stale-counter clear with +0x202 preserved,
unbound NULL-safety).

### FUN_0046826c — secondary-settlement 2×2 stamp ✅ ported

Another per-realm leaf of `FUN_0049bd99` (called once per realm whose
banner-level field +0x1c0 is non-zero, with `level` = that field − 1).  The
parallel of the capital placer `FUN_00467fd1`: where that stamps the capital
2×2 block, this stamps the realm's **secondary** cell (record +0x78,
`ARMY_SEC_OFF`).  It's a thin tier-selector over the shared
`engine_territory_stamp_block`: descriptor base is `'P'`(0x50) /
`'('`(0x28) / `'<'`(0x3c) chosen by the realm's settlement fields (+0x1c3
present-flag, +0x1c4 size `< 0x32`) plus a `level*4` stride, and the +0 TYPE
is `0x15 + min(level, 4)`.  Ported as `engine_territory_stamp_secondary_keep`;
4 host tests (the three tiers, the level cap at type 0x19, unbound no-op).
No new globals — the two tier fields and the secondary offset all live in the
already-bound `g_per_army_768`.

### FUN_0044f6e7 — per-realm population / job-slot allocator ✅ ported

The 2 KB zero-dep leaf of `FUN_0049bd99` (called **twice** per realm there,
with the intervening 0x44df5f / 0x4485a5 passes re-tuning the caps between the
two distributions).  It allocates the realm's population across eight capped
job-slot counters in `g_per_army_768`.

The realm's total population (record +0x24) is split by the signed-byte
percentage at +0x08 into two pools — `pool1 = total*pct/100` and `pool2 = the
remainder` — distributed in two phases.  The eight counters and their caps are
a regular run of 0xc-byte triples at +0xc4 (counter `i` at +0xc4+0xc*i, its cap
at +0xcc+0xc*i = counter+8); the ninth 0xc-slot at +0x124 is the **leftover**
(no cap).  The eight target ratios are a separate i32 run at +0x130 (ratio `i`
at +0x130+4*i, paired with counter `i`).

  - **pool2 → buckets 0,1,2** (ungated baseline jobs).  Greedy pre-fill toward
    `percent_of(pool2, ratio_i)`, then a round-robin weighted **2 : 3 : 1**.
  - **pool1 → buckets 3..7** (settlement/building jobs).  Each bucket's cap is
    forced to 0 unless its "present" flag is set: buckets 6/4/7/5 by the
    stride-0x18 settlement flags at +0x297/+0x2af/+0x2c7/+0x2df (the same flags
    `FUN_0049bd99` lights at lines setting +0x297+c*0x18), and bucket 3 by
    `ARMY_SETTLE_FLAG` (+0x1c3) **and** +0x1b0.  Greedy pre-fill, then a
    round-robin dealing 6,5,4,7 once each with bucket 3 as the catch-all.

Between the phases a small pool-2 remainder (`< the +0xb8 threshold byte`) is
folded back into pool1 so it can still feed the building buckets.  Whatever is
left of both pools lands in +0x124.  Caps carrying retail's `999999`
"unlimited" sentinel are read as 0 (so the `counter < cap` test never fires —
the same no-fill the engine relies on).  The only callee is
`engine_math_percent_of` (FUN_00404d6b); it touches nothing but the realm's own
record row.  Ported as `engine_territory_allocate_realm_workforce`; 7 host
tests (phase-1 greedy split, pct pool-split feeding both phases, building-gate
spilling to leftover, the 2:3:1 round-robin weights, the small-remainder fold,
the 999999 sentinel cap, unbound no-op).  No new globals.

### FUN_00469d21 — band-retype an army's feature cells ✅ ported

The single callee of `FUN_0044d913` (and of the unrelated `FUN_0044c8ae`), now
a leaf because its own callee `FUN_0046d7f4` (the cell terrain-type setter) is
ported.  It walks all 4096 overworld cells and, for each cell owned by `key`
(byte +7), flagged active (byte +1 bit 0x20), with its TYPE byte (+0) in a
band [lo, hi], rewrites the cell's terrain type via
`engine_territory_set_cell_type`.  The **first** match always becomes
`(type, variant)`; for every later match, when `lo == 2` and the realm's +0x1a7
flag is set, the cell collapses to base type 2 / variant 0 instead — so a
band's first feature cell keeps the requested type and the rest revert.  The
`0x44d913` caller passes `lo == 0x13`, so the special case is dormant on the
new-game path; the `0x44c8ae` caller passes `lo == 2` and drives it.  Ported as
`engine_territory_retype_army_band`; 4 host tests (predicate filtering, the
lo==2 collapse, lo==2 with the flag clear keeping all, unbound no-op).  No new
globals.

### FUN_0044d913 — grade feature cells by population density ✅ ported

The per-realm callee in `FUN_0049bd99`'s first loop, run right after the
terrain census seeds +0x200.  Now a leaf (its only callee `FUN_00469d21` above
is ported).  It computes a density = load(+0x250) per high-terrain cell (the
census HIGH count at +0x200, = `ARMY_CENSUS_HIGH`): 0 when there is no load, a
saturated 1000 when there is load but no high cell to divide by, else the
integer quotient.  That density picks a feature-TYPE tier (no load → 0x13,
density <0xb → 0x14, <0x15 → 0x15, else 0x16), applied to the realm's
band-[0x13,0x16] cells via `engine_territory_retype_army_band`; a parallel
ladder writes a derived value to +0x25c (<0xb → 10, <0x15 → 0x14, <0x1f → 0x1e,
else 0x28, forced to 0x28 when there is no high cell).  Ported as
`engine_territory_grade_feature_cells`; 4 host tests (no-load tier, the density
tier/value ladders, the saturated no-high case, unbound no-op).  No new globals.

**Remaining `FUN_0049bd99` leaves** (bottom-up): the 1-dep helpers
`FUN_00432746` / `FUN_0046dfd5`, plus the two cap-tuning passes interleaved with
the allocator (`FUN_0044df5f` / `FUN_004485a5`).  With those, every per-realm
callee of `FUN_0049bd99` is ported and the setup pass itself can be assembled.

5. **Assemble `engine_game_init`** (FUN_00497ced orchestrator) once its
   callees are real; then `FUN_00499dc3` + `FUN_004329ec` spine + the
   `DAT_0052af94=3` flip.
6. **Strategic-map screen arm** of FUN_0040f1a0 — the render half.

Only after 5+6 does clicking Start show a live map.
