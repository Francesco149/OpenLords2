# The battlefield port — the tactical battle under rhost (2026-11-04)

Goal: port the whole tactical battle (screen 0x12; frame-driver sub-state DAT_0057c8fc == 2) so the standalone
can play a battle. Before this the port had only its renderer (`engine_battlefield_render.c`) and the exits
(`engine_combat.c`, batch 3); `engine.c` leaves the combat cluster as a TODO. Corpus reach (`rhost.py coverage`,
2026-11-04): the 10 `bot3*` scenarios fight on the field; 173 of the 235 battlefield functions are reached.

Older RE of the subsystem: `combat-battlefield-ai.md` (unit / group records, the sim chain), `combat-battlefield-input.md`,
`battlefield-render.md`. They predate rhost — trust retail, not them.

## Layout

- **One canonical name per function: `bf_<va>`** (6 hex digits, lowercase), retail arity, `int` return to start.
  `tools/bf/parts.py` assigns each to ONE partition; `engine_bf_<part>.h` declares the partition's functions.
- **Not ported yet = a stub** in `src/engine_bf_<part>_pending.c`: `engine_retail_call(va, n, …)`
  (`src/engine_retail_call.h`). rhost binds it to retail's own code (cdecl, bracketed by
  `rh_port_retail_enter/leave`, `tools/rhost/rh_hooks_bf.c`); the standalone stops with the VA. So a port
  function calls its callees by name whether they are ported or not, and the partitions proceed in parallel.
- **Porting a function** = delete its stub line, define it in `src/engine_bf_<part>.c` with a
  `/* Port of FUN_00xxxxxx (N B) — what it does */` comment (that token is what the ledger counts), add a row to
  `tools/rhost/rh_hooks_bf_<part>.c`.
- **Shared header** `src/engine_bf.h`: includes the save block, `engine_bf_globals.h`, every partition header;
  record accessors that take the FIELD'S RETAIL VA as IDA prints it:
  `byte_5544AC[432*i]` → `BU8(i, 0x5544ac)`, `word_554620[216*i]` → `BS16(i, 0x554620)`,
  `dword_55449C[108*i]` → `BS32(i, 0x55449c)` (units, 0x554480, stride 0x1b0); `BG*` groups (0x566520, 0x34);
  `BC*` combat units (0x57a100, 0x4c); `BCELL8(off, 0x5440e7)` the 80×80×8 cell grid (`g_castles_state`, 0x5440e0;
  a unit's cell offset `dword_55449C` is a byte offset into it); `AT(T, sym, symva, va)` anything else.
- `bf_rng_step()` (engine_bf_common.c) = FUN_00404a46 on retail's globals.

## Globals

`src/engine_bf_globals.h` (GENERATED, `tools/bf/gen_skeleton.py --globals-only`): every retail global the
battlefield touches that had no port symbol, as `g_bf_<va>` — a scalar IDA never indexes is its own typed symbol;
arrays / a struct array's fields form one `uint8_t` block (`AT(int16_t, g_bf_50402e, 0x50402e, 0x504030 + 2*i)`).
rhost aliases each at its VA (`tools/rhost/rh_bf_alias.h`), so under rhost every access is exact whatever block
it goes through. Already owned by another port symbol — use that one: the save block (`s_slot_<va>`,
`g_per_battle_hex_432` …) and the aliased runtime globals (`engine_combat.h` `g_combat_*`, `engine_path.h`,
`engine_map_query.h` …). Accessors bridged across the link cut: `engine_current_screen()` /
`engine_set_current_screen()` (DAT_004eac50), `engine_set_demand_redraw()` (DAT_0057d340).

Flags in the header comments:
- **NEIGHBOUR of X** — IDA's name for a grid's neighbour (`byte_543E61[off]` = the cell grid at off − 0x27f, the
  row above): access it through X (`BCELL8(off, 0x543e61)`), never through the block — the standalone keeps
  the blocks separate.
- **DUP** — another src/ module already names this VA (a file-static, or a module rhost does not link). Fine
  under rhost; the standalone must reconcile it with that module before the battlefield is wired in.

## Porting loop (per function)

1. Read `/mnt/ephemeral/openlords2/decompiled-ida/all_ida.c` (`/* ===== sub_XXXXXX @ … */`) **and the disasm**
   (`all_ida.asm`, `; ===== sub_XXXXXX`): IDA's pseudocode hides signedness (`movsx` / `movzx`, `jl` vs `jb`),
   byte vs dword stores, and re-reads of globals after calls.
2. Write the port on retail's layout. Re-read a global after every call (callees change the loop indices
   DAT_0053e8f8 & co. — retail's loop variables are globals; keep them globals). Keep every write retail makes
   (the shadow compares every state dword), including scratch globals.
3. Row: `HOOK_V(bf_<va>, N, bf_<va>(I(0), …))` when no caller reads EAX, else `HOOK_R` (compared).
4. `python3 tools/rhost/rhost.py verify --target wine -j 4 --only bot3 --hooks '^bf_'` — shadow passes +
   the hybrid pass over the battle scenarios (`--hooks` filters rows; it writes no evidence). Iterate on one
   hook: `--hooks '^bf_4822ed$'`. A DIVERGE line names the first differing VAs: attribute (stack residue? a
   signed byte? an unread global?) before touching logic. `rhost.py run SCEN --target wine --extra "--trap VA"`
   / `--watch-write VA@F` probe retail.
5. Commit with the evidence line.

Shadow cost is the pages a call writes: the per-tick drivers (138k calls / corpus) verify in ~1.5 min.

## Pilot (2026-11-04)

FUN_00478952, FUN_004822ed (the per-unit driver: dispatch table → stubs → retail), FUN_0046ebe4, FUN_0046eb3f:
`verify --only bot3 --hooks '^bf_'`: 4/4 reached, 0 divergences (bf_4822ed 138132 calls / 10 scenarios),
hybrid PASS.

## Status (2026-11-04, 11:05)

**All 260 battlefield functions ported** (8 partitions; the 4 `extern` seams stay retail: audio stream, sprite
loads), each shadowed by its owner over `bot3*` (14 scenarios, 0 divergences; per-partition evidence in
`bf-<part>.md`). **Combined hybrid**: all 260 rows `replace` at once, `corpus --check --target wine` over the whole
corpus (51 scenarios): **PASS** — every state hash and every presented frame bit-exact. The battle sim runs on the
port inside retail. Unverified (the corpus never calls them; ported from the asm; many shadowed on poked state —
exploration evidence only): listed per partition in the merge commits and the docs (~45 functions: the 3×3 siege
engines' steps, the defender stance table, orders 0 / 7 / 9 / 12–16, a wall breaking, wood burning).

Lessons the partitions hit (now in the porting loop):
- A global another linked port mirrors (`g_path_dist_dx/dy` 0x591560 / 0x58fea0, `g_cell_cursor_col/row`
  0x569570 / 0x569578, the dirty-rect gate 0x53e9ac): use the owning port symbol — inside a port call the
  mirror is the live copy (rhost syncs it to retail only at exit / a retail call); rh_port_sync_out stores a
  mirror only when the port changed it, so a direct alias write survives, but a READ of the alias is stale.
- Stack residue: the class 7..10 sequencers store an uninitialized local (unit: `bf_unit_res_*` seams).
- Callee tokens: name callees by bare hex in comments (`tools/bf/fix_tokens.py` at every merge).
- A worktree needs untracked symlinks: `vendor`, `docs/decompiled`, `src/gen` (generated headers).

## Partitions

`python3 tools/bf/status.py [-v]`.

| part | what | functions | bytes |
|---|---|---:|---:|
| move | the move order: the path search / step (FUN_0048f1dd and below) | 26 | 19264 |
| strike | melee / missile strikes, damage, impact effects (FUN_00483337, FUN_00492c8b) | 22 | 12993 |
| unit | the per-unit driver, movement-state table DAT_004d9140, animation sequencers | 31 | 10997 |
| action | the order table DAT_004d9170 + private helpers | 36 | 18470 |
| group | group AI FUN_00489401 + interactive stances DAT_004d91b8, aggregator, projectiles, fire | 42 | 17158 |
| stance | the other stance tables DAT_004d91f8 / DAT_004d91d0 | 28 | 12140 |
| setup | entering the battlefield: terrain, deployment, spawn (FUN_004778a0) | 50 | 26085 |
| input | the battlefield input (screens 0x28..0x2a: select, order, control panel) + HUD panels (FUN_0043bf07 …) | 25 | 7856 |
| extern | the out-of-scope seams: the tick reset 0x43f304, the click cue 0x499d97, the battle music 0x477b2f, the sprite loads 0x4987b7 (32 table slots by battle kind / nations, misc_bat.PL8 at 0x4daec4 — not 0x4daeb0 misc_cty.pl8, the record before it): all ported | 4 | 2006 |
