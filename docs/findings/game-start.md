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
| FUN_004676e0 | 144 | 11 | overworld-grid setup sequence — 3 leaf callees ported (see below); 5 callees remain |
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

### FUN_004676e0 — overworld-grid setup sequence (2/8 leaves ported)

A 144-B 0x497ced callee that stands up the overworld cell grid + its
terrain index tables, by calling a fixed sequence of 0x46c*/0x46d* grid
helpers (`FUN_0046c5b8`×6, `FUN_0046df51`, `FUN_0046dbf5`, `FUN_0046dd34`,
`FUN_0046da4b`, `FUN_00467ca6`, `FUN_00468d4f`, `FUN_00467a36`) plus the
already-ported `FUN_00467770` / `FUN_0046a037` / `FUN_004291b3`.  Two of
its leaf callees landed this chip:

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

Remaining 5 unported FUN_004676e0 callees: `FUN_0046dbf5` /
`FUN_0046dd34` / `FUN_00467ca6` / `FUN_00468d4f` / `FUN_00467a36` (each
still has 1–5 unported deps).

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

5. **Assemble `engine_game_init`** (FUN_00497ced orchestrator) once its
   callees are real; then `FUN_00499dc3` + `FUN_004329ec` spine + the
   `DAT_0052af94=3` flip.
6. **Strategic-map screen arm** of FUN_0040f1a0 — the render half.

Only after 5+6 does clicking Start show a live map.
