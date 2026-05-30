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
| FUN_00427ed0 | 311 | 3 | (unsurveyed) |
| FUN_0042847c | 82 | 0 | copy DAT_004d8910 table → DAT_005532a0 strided |
| FUN_00448440 | 261 | 29 | (unsurveyed — large fan-out) |
| FUN_00451150 | 936 | 7 | (unsurveyed) |
| FUN_004676e0 | 144 | 11 | (unsurveyed) |
| FUN_0046f1dc/2ae/3cd | — | — | (the trio above) |
| FUN_0046ff43 | 617 | 0 | leaf seeder (unsurveyed) |
| FUN_00472ad0 | 16 | 1 | **tested** (already ported) |
| FUN_00497e65 | 1025 | 1 | per-player RNG seat assignment |
| FUN_004984dc | 731 | 9 | (unsurveyed) |
| FUN_00499aca | 487 | 2 | (unsurveyed) |
| FUN_00499d7d | 26 | 1 | play click3.wav (audio) |
| FUN_0049a453 | 302 | 2 | **tested** (already ported) |
| FUN_0049aa0e | 2197 | 1 | (unsurveyed — large) |
| FUN_0049b35e | 205 | 1 | (unsurveyed) |
| FUN_0049bc5f | 314 | 0 | **tested** (already ported) |
| FUN_0049bd99 | 3063 | 9 | (unsurveyed — largest) |
| FUN_004a1c53 | 537 | 2 | (unsurveyed) |
| FUN_004ac904 | 295 | 1 | (unsurveyed) |
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
   - ⏸ **FUN_004ae74d** — **deferred.**  Clears the `DAT_0056d8b8`
     region (r2: outer stride 0x80, dword+byte per 8-B element, els
     1..6400 with a deliberate 8-B tail overrun).  The region is
     aliased across unported code three ways: a scalar cursor
     (`=10`/wrap-63 in 0x46a295), a freeable pointer at +4
     (`DAT_0056d8bc`, freed in 0x49915e / set 0x4990f9), and the
     payload array (0x4156a7 / 0x4ae7dd).  Its size + element struct
     can't be homed without desync risk until a reader subsystem lands;
     defer until then.

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
4. **Heavy seeders** — FUN_00497e65 (RNG seats), FUN_0049bd99,
   FUN_0049aa0e, FUN_004984dc, FUN_00451150, FUN_00448440 (surveyed
   one at a time).
5. **Assemble `engine_game_init`** (FUN_00497ced orchestrator) once its
   callees are real; then `FUN_00499dc3` + `FUN_004329ec` spine + the
   `DAT_0052af94=3` flip.
6. **Strategic-map screen arm** of FUN_0040f1a0 — the render half.

Only after 5+6 does clicking Start show a live map.
