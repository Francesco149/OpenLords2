# OpenLords2 — porting roadmap (next chips)

> Hand-maintained, **seeded by the `ol2-frontier-triage` survey workflow**
> (2026-05-29, 16 read-only agents over the top `port-frontier.md` leaves).
> The mechanical frontier lives in [`port-frontier.md`](port-frontier.md)
> (derived); this file adds the *semantic* triage — subsystem, port
> difficulty, target module, gotchas — that can't be derived. Update by
> hand as chips land; re-run `gen_frontier.py` to refresh the candidate set.

**difficulty:** `mechanical` = verbatim leaf port · `moderate` = maps a
struct or several globals · `hard` = jump table / float math / unknown layout.
**order:** 1 = port first … 5 = defer.

> ⚠️ **Scalar-global wall (2026-05-29):** leaves tagged `[scalar-blocked]`
> read/write engine globals that exist only as save-private
> `static s_slot_<va>` mirrors in `src/save_schema_table.h` (202 of them).
> Porting them now creates storage that desyncs from the save system —
> see PROGRESS 2026-05-29.  Blocked until the runtime-global ownership
> model is decided.  Leaves touching only the 13 *shared* big blocks
> (extern in `save.h`) or gfx.c-owned DAT mirrors are unaffected.

| ord | VA | size | callers | difficulty | subsystem | → target module |
|:---:|----|-----:|:-------:|------------|-----------|-----------------|
| 1 | `0x41423f` | 11 | 1 | mechanical | render-quad / blit primitives | `src/gfx.c` — *port with `engine_tick` body* |
| ✅ | `0x42eaa6` | 214 | 2 | mechanical | gfx | **DONE** → `gfx.c` `gfx_release_surfaces` (2026-05-29) |
| ✅ | `0x49caaa` | 935 | 2 | moderate | npc-schedule | **DONE** → `engine_npc_schedule.c` `engine_npc_schedule_init` (2026-05-29) |
| 2 | `0x4360f2` | 41 | 1 | mechanical | stage/post-load + char tables | `src/engine.c` `[scalar-blocked]` |
| 2 | `0x49bbe8` | 53 | 1 | mechanical | castle-army-setup | `src/engine_castle_data.c` `[scalar-blocked]` (count = entry 141) |
| 3 | `0x4af502` | 72 | 7 | mechanical | io-helpers | `src/io.c` — *tier-2/3 deferred; dead today* |
| 3 | `0x4c0580` | 25 | 2 | mechanical | memory-management | `src/io.c` |
| 3 | `0x42e26f` | 41 | 2 | mechanical | smacker-playback | `src/engine_smacker.c` |
| 3 | `0x42f58c` | 130 | 2 | moderate | graphics-gfx | `src/gfx.c` |
| 3 | `0x43fd2a` | 413 | 2 | moderate | gfx-sprite-cache | `src/gfx_sprite.c` |
| 3 | `0x47f474` | 761 | 2 | moderate | battlefield-state | `src/engine_battlefield_render.c` |
| 3 | `0x401c5b` | 21 | 1 | mechanical | text-input | `src/text_input.c` |
| 3 | `0x49ca32` | 46 | 1 | mechanical | game-init | `src/game_init.c` |
| 4 | `0x480f8b` | 1200 | 2 | moderate | battlefield-render | `src/engine_battlefield_render.c` |
| 4 | `0x45242b` | 11 | 1 | mechanical | fade / loading / audio-mci | `src/engine_fade.c` |
| 5 | `0x4b1dd5` | 11 | 1 | mechanical | battlefield-render | `src/engine_battlefield_render.c` |

## Per-function notes

### `0x41423f` — render-quad / blit primitives (order 1, mechanical)

No-op stub function that returns immediately without any side effects.

- **target:** `gfx.c` · 1 ported caller(s) · 11 B

### `0x42eaa6` — gfx (order 2, mechanical)

Graphics subsystem teardown: releases DirectDraw objects (clipper, back/primary surfaces, DD instance) and optionally logs via the debug formatter.

- **target:** `src/gfx.c` · 2 ported caller(s) · 214 B
- **globals:** DAT_004ea12c (g_clipper), DAT_004eafac (g_back), DAT_004eabd0 (g_primary), DAT_004ea1a8 (g_back_pitch), DAT_004e6974 (g_dd)

### `0x49caaa` — npc-schedule (order 2, moderate)

Initialize NPC worker schedules and walking paths for armies 1-5 by assigning available workers to job slots and work locations

- **target:** `engine_npc_schedule.c` · 2 ported caller(s) · 935 B
- **globals:** DAT_00553d75/76 (army availability flags), DAT_0057bf04/05/07/08/09/0a/2a/29 (worker state arrays), DAT_0053f268 (worker count limit), DAT_0053f034 (difficulty variant), DAT_00553030/0055302c (difficulty/variant flags), DAT_004dc17c/1ce/1cf (position lookup tables), DAT_005c9270 (localization walker)
- **gotchas:** Accesses multiple global arrays with 0x160-byte stride (352 bytes per army, armies 1-5); calls localization_seek_skip and memcpy which are already ported; lookup table indexing at DAT_004dc17c uses multiplication by 4 per job slot (0-3); algorithm uses local byte arrays for tracking assigned resources; stride and offset calculations are critical to correctness

### `0x4360f2` — stage/post-load + char tables (order 2, mechanical)

Initialize game state by clearing two timers/counters and setting render-dirty flag to 2 (ready to render).

- **target:** `src/engine.c` · 1 ported caller(s) · 41 B
- **globals:** DAT_0057a0cc, DAT_005cd41c, DAT_0057d340
- **gotchas:** None — pure leaf with trivial global assignments.

### `0x49bbe8` — castle-army-setup (order 2, mechanical)

Store a byte-pair entry at index param_2 into two 6-byte arrays and increment a counter when param_2 is non-zero

- **target:** `src/engine_castle_data.c` · 1 ported caller(s) · 53 B
- **globals:** DAT_00568db0, DAT_00568db1, DAT_0053e8a4
- **gotchas:** Related functions FUN_0049bc1d, FUN_0049bc5f, and FUN_0049bd99 form a cluster managing the same byte-pair arrays; should be ported together or with clear separation of concerns. Parameter indexing uses param_2 * 2 to achieve pairing of two consecutive bytes in each array.

### `0x4af502` — io-helpers (order 3, mechanical)

Change working directory back to base directory (DAT_0058fe30); log failure via debug logger if chdir fails

- **target:** `src/io.c` · 7 ported caller(s) · 72 B
- **globals:** DAT_0058fe30 (100-byte base dir path buffer), DAT_004e6910 (100-byte working dir buffer)
- **gotchas:** Depends on FUN_004afab9 (debug logger, not yet ported). Function does not early-return on chdir failure; caller must handle the condition. Already called by 7 ported functions despite "no consumer" status in older docs.

### `0x4c0580` — memory-management (order 3, mechanical)

Debug memory deallocation wrapper; calls __free_dbg with second parameter hardcoded to 1.

- **target:** `io.c` · 2 ported caller(s) · 25 B
- **gotchas:** __free_dbg is a Visual C++ debug runtime function; port must replace with standard free(). Function is trivially simple (one call, one return).

### `0x42e26f` — smacker-playback (order 3, mechanical)

Query whether a Smacker video is currently playing by checking the global Smacker playback flag

- **target:** `engine_smacker.c` · 2 ported caller(s) · 41 B
- **globals:** DAT_00516a4c (Smacker playback state flag, set/cleared by Smacker playback control functions)

### `0x42f58c` — graphics-gfx (order 3, moderate)

Update DirectDraw palette with either 8-bit or 16-bit palette data based on current display mode

- **target:** `src/gfx.c` · 2 ported caller(s) · 130 B
- **globals:** DAT_0053f228 (display mode flag: 8bpp=0 vs 16bpp=1), DAT_004df288 (IDirectDrawPalette ptr), DAT_00515d90 (8bpp PALETTEENTRY array), DAT_005165b0 (16bpp PALETTEENTRY array)
- **gotchas:** Function uses indirect vtable calls through DAT_004df288 (offset +0x18 = SetPalette). Depends on FUN_0042f449 and FUN_0042f306 to have already populated the palette data arrays. Two different palette arrays (DAT_00515d90 vs DAT_005165b0) based on mode flag.

### `0x43fd2a` — gfx-sprite-cache (order 3, moderate)

Compute byte-sum hash of sprite frame cache data for the current player slot and game mode (strategic or combat).

- **target:** `src/gfx_sprite.c` · 2 ported caller(s) · 413 B
- **globals:** DAT_00553030 (game-mode flag), DAT_0057c8fc (player slot), DAT_00512f60 (frame state array), DAT_00512f40 (frame state array), DAT_005158d0/d4 (frame cache pointers), DAT_00512f90/0050d7b0 (hash output arrays), 0x4d5b10/18/14 (frame stride/offset table)
- **gotchas:** The function uses strided pointers to frame data structures with hardcoded offsets (0x4d5b10/18/14) that appear to define frame descriptor layouts. The nested loop structure with dynamic pointer arithmetic over frame bytes requires understanding the frame format. DAT_0057c8fc indexes into multiple arrays with stride 4, requiring care with boundary checks. The function toggles between two frame sources (DAT_005158d0 vs d4) based on state in DAT_00512f60.

### `0x47f474` — battlefield-state (order 3, moderate)

Clear and reset player troop counters during battle state transitions (entity count accumulation reset)

- **target:** `engine_battlefield_render.c` · 2 ported caller(s) · 761 B
- **globals:** DAT_0052f218, DAT_0052f246, DAT_0052f21c (per-player troop counters), DAT_00568210, DAT_00568218 (active player IDs), DAT_005544ac, DAT_00554492, DAT_00554494, DAT_00554620 (entity array and properties)
- **gotchas:** Heavy pointer arithmetic with strides 0x1a4 and 0x1b0 requires accurate struct offset mapping. The loop boundary (0x50 = 80 entities) is hardcoded. DAT_00554494 comparison relies on entity property byte meaning. The function does in-place pointer casts (undefined4, undefined2, undefined1) that must map to appropriately-sized struct fields.

### `0x401c5b` — text-input (order 3, mechanical)

Set a global delay or timer counter to 100 (called on specific keyboard input)

- **target:** `src/text_input.c` · 1 ported caller(s) · 21 B
- **globals:** DAT_005cd41c (some kind of delay/timer counter, set to 100 here, cleared elsewhere)
- **gotchas:** DAT_005cd41c is not yet documented in the globals mapping. The function appears to be a leaf that initializes some kind of input delay counter, but the exact purpose (debounce timer, repeat rate, etc.) is unclear from the decompilation alone. Two identical copies exist at 0x401c5b and 0x4342f3.

### `0x49ca32` — game-init (order 3, mechanical)

Initialize per-nation/player bit flags (1, 2, 4, 8, 0x10) in five globals spaced 0x160 bytes apart.

- **target:** `src/game_init.c` · 1 ported caller(s) · 46 B
- **globals:** DAT_0057c14e, DAT_0057c2ae, DAT_0057c40e, DAT_0057c56e, DAT_0057c6ce (5 sequential nation/player flag globals)
- **gotchas:** None — pure leaf assignment with no function calls or branching. Globals need declaration/mapping to named equivalents (likely per-player/per-nation flags in g_player_strategic_352 schema entry 3).

### `0x480f8b` — battlefield-render (order 4, moderate)

Initialize sprite blob pointers for battlefield combat entities and effects based on entity type and state.

- **target:** `engine_battlefield_render.c` · 2 ported caller(s) · 1200 B
- **globals:** g_per_battle_hex_432, g_per_combat_unit_76, multiple sprite blob addresses (DAT_0053ef50, DAT_0053f0d0, DAT_0056d640, DAT_0056d684, DAT_0057c8d4, DAT_0052afb0, DAT_0053f650, DAT_00553250, DAT_0055ce58, DAT_00553f2c, DAT_0055cd30, DAT_0056d890, DAT_0057c8bc, DAT_0057cb28, DAT_0053f0bc, DAT_00553258, DAT_00566518)
- **gotchas:** 10+ hardcoded sprite blob addresses require retail-to-port mapping; entity type dispatch (0x00-0x0a) needs semantic identification; offset 0x2c indicates active flag, 0x12 is entity type, 0x18 may indicate state/facing; function operates only in combat mode (DAT_0057c8fc == 2); neighboring FUN_00480f2e performs related cleanup

### `0x45242b` — fade / loading / audio-mci (order 4, mechanical)

Empty stub function that returns immediately (no implementation).

- **target:** `src/engine_fade.c` · 1 ported caller(s) · 11 B
- **gotchas:** This is a pure no-op stub (11 bytes: just a return instruction). The caller FUN_004b99c0 (engine_tick) is already ported and unconditionally invokes this at line 170, so a matching no-op is required to maintain call site fidelity. The retail binary likely uses this function as a hook point for fade/loading/audio-related logic that was disabled or stubbed out in retail.

### `0x4b1dd5` — battlefield-render (order 5, mechanical)

Empty stub function that does nothing (void body with immediate return)

- **target:** `engine_battlefield_render.c` · 1 ported caller(s) · 11 B

---

## Notable findings from the survey

- **Three of these are pure no-op stubs** (`0x41423f`, `0x45242b`,
  `0x4b1dd5`, all 11 B = a bare `ret`). Retail keeps them as call-site
  hooks for logic disabled before ship. Port them as matching no-ops to
  preserve call-site fidelity (the diff harness sees the `call`).
- **`0x4af502` (chdir-back-to-base) now has 7 ported callers** despite the
  2026-05-26 PROGRESS dispatch deferring it as having "no consumer today".
  It is the single most-wanted leaf — but it depends on `FUN_004afab9`
  (status.txt debug logger, unported), so landing it pulls that in or stubs
  the log. Revisit the IO-cluster deferral in light of this.
- **`0x4c0580` is a `__free_dbg` wrapper** — port as plain `free()`; the MSVC
  debug-heap second arg (block-type 1) has no portable meaning.
- **The battlefield-state/render leaves** (`0x47f474`, `0x480f8b`) carry the
  combat entity struct (strides `0x1a4`/`0x1b0`/`0x160`); mapping that
  layout once unblocks a cluster of follow-on combat functions.
