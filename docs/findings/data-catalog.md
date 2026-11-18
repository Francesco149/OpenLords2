# The data image catalog — what documenting it turned up

`docs/data/catalog.tsv` (hand-written rows) → `tools/data_ledger.py` → `docs/data-ledger.md`. 2026-11-17: **.rdata and
.data 100 % documented** (every non-zero byte under a catalog row; 166 rows added that day, 397 in all; all sections
85.0 % — the rest is PE headers / imports / resources, named by structure). The rows cite the consuming IDA function(s)
and the port that reads each item.

## Port defects found

- **rl_476da9** (src/rl_turn.c) read `byte_4D6ED8[report]` as an offset into `g_rl_4d6e88` (0xa8 B, ends 0x4d6f30);
  the reports 200..218 land at 0x4d6fa0.., inside `g_rl_4d6f30` (k_hint_report_panel). It passed 0.76 M shadowed
  calls only because the linker placed the two blocks adjacently. Fixed: index the block the VA falls in.

## Retail reads past a table (quirks, ported faithfully)

- 0x412b33 `dword_4D2950[task]` for task 9 (9 entries) → 0x4d2974[0] = 0 (no sound cue).
- 0x401666 atoi: 9+ digits index k_pow10 past its end into the ERR strings.
- castle level 0: `0x4d8a0c[level]` (garrison cap, 0x437afb / 0x47309e / 0x4a79a3 / 0x4a9372) reads 2500, the last
  k_castle_work dword.
- 0x48895e: phase 160 → 0x4d9c08[40], one byte past, = 0x4d9c30[0] = 0.
- 0x4e3098 (cursor per screen) has 70 dwords; screens 0x46 / 0x47 would read k_screen_sim_gate (highest seen: 0x45).
- 0x4e2228 (narration 5-byte rows, events 200..220): the last row has 4 bytes; its 5th is unreachable (row starts 0).
- open: 0x4a5003 indexes 0x4d8778 by percent_div(50, population) — a county under 50 people would pass index 101
  (into 0x4d8910); callers not checked.

## Dead data (no reader: no xref, no pointer value in the exe, no indexed base reaches it)

0x4d2170 (8-direction dx/dy), 0x4d4d50, 0x4d54c8, 0x4d5b70 (8 addresses parallel to the MP desync table), 0x4d6578
(a 100..125 curve), 0x4d6870, 0x4d7f80, 0x4d8950, 0x4dc280; behind dead code: 0x4d55e0 (0x43cca5, callers 0x43cc66 / 7b
/ 90 have none), 0x4d80e8 / 0x4d80f8 (0x46a578 / 790 / 9c7), 0x4d9a78 / 0x4d9aa0 / 0x4d9b68 / 0x4d9bb8 (0x4867cc /
0x488240 have no caller). CRT: 0x4e52ac (a reloc'd `__CxxUnhandledExceptionFilter` pointer; 0x4cc950 uses an
immediate), `_pwctype`, `__decimal_point(_length)`, `_cfltcvt_tab` 2 / 4 / 5, errtable's second error-6 entry.

## Port names / comments that the catalog contradicted — done

Renamed 2026-11-18 (commit "rename pass: port names / comments the data catalog contradicts"; each row re-checked
against the IDA export; objects byte-identical before / after). Old → new:
- rl_render.c rl_41608b "workforce category tooltip (screen 0xc)" → the merchant grid's hovered-good tooltip — screen
  **8** (the row's "screen 0x0c" was wrong: 0x41608b runs under DAT_004eac50 == 8, 0x41635f trade_content under 12)
- engine_bf_stance.c "MP DAT_0057a0f0", "the realm's DAT_004d4b18" → standalone skirmish battle; battle DAT_0056d590
- engine_combat.c: k_siege_holds → k_siege_battle_holds (16 per battle); k_keep_tbl → k_castle_garrison_grant (0..5;
  [7..11] = 0x4d8a58's head, size kept); k_muster_cost → k_conscript_happy_cost, `supply(_cost)` → `happy_cost`,
  A_F0C / A_F15 / A_PROV24 / A_F38 → A_HAPPY / A_HAPPY_ARMY / A_POP / A_ARMY_CHANGE, `min_prov` → `min_pop`,
  spawn_debit_provision → spawn_debit_population; k_cond_rate → k_siege_engine_work, C_COND_CNT / OUT / STOCK /
  LEVEL → C_SIEGE_CNT / PCT / PROG / LEFT; U_CURSOR = county cursor
- engine_npc_schedule.{c,h} → engine_realm_personality.{c,h} (engine_realm_personality_init / _set_localization,
  s_job_table → s_personality_pref; tests/test_engine_realm_personality.c, test_personality_*)
- rl_turn_globals.h g_rl_4d8a3c comment → garrison grants; engine_army_ai.c k_keep_cost[0] comment → k_weapon_cost[5]
- engine_army_state.c k_facing_march / _idle → k_step_frame_march / _idle, A_FACING (+0x1b) → A_STEP, A_ANIM (+0x09)
  → A_FACING, anim_phase → facing_slot; engine_army_move.c M_FACING → M_STEP; rl_terr.c FACING_MARCH / _IDLE →
  STEP_FRAME_MARCH / _IDLE (port tables keep 8 of 16 entries, index & 7 — unchanged)
- engine_realm.c k_pop_deathmod_tbl / K_POP_SEASON_MOD → k_pop_death_health / K_POP_DEATH_SEASON
- 0x4d8990: engine_labor.c k_labor_weapon_rate / _cost → k_weapon_wood / _iron; engine_army_economy.c k_race_pool_a /
  _b → k_weapon_wood / _iron, ARMY_RACE → ARMY_WEAPON_KIND, RESPOOL_RACE_ACC → RESPOOL_WEAPON_ACC; engine_army_ai.c
  g_ai_build_cost → k_weapon_cost, ARMY_BUILD_TAG → ARMY_WEAPON_KIND
- 0x4de79f: game_init.c k_unit_col_a → k_merc_start_county; catalog k_merc_period → k_merc_start_county
- engine_army_ai.c STRAT_AI_LEVEL → STRAT_COUNTIES (resolved: 0x49d1e0 recounts it, 0x4a72fe adds 1 per conquest)
- rl_crt.c / rl_crt_dbgheap.c G_SBH_HEAPHDR → G_SBH_HEAD_REGION
- g_rl_4e4520[0x60], rl_ctl_globals.h g_rl_4d6a78[5] → comments; the extern now [8] (32 B); the 0x60 definition kept

## The port's own copies vs the exe (2026-11-17, `tools/data_selfcheck.py` → `docs/data-selfcheck.md`)

Under rhost / the standalone every port global aliases its retail VA, so the initializers in `src/*_globals.c` are never
linked there — nothing checked them. The tool compiles `src/` with `-fdata-sections`, takes each aliased name's
definition (exact size by a `sizeof` probe TU: COFF pads section sizes to the alignment), and compares it with the exe
at the VA; relocated slots are resolved through the aliases / `rl_va_map.c`. Then a content pass looks for still
uncovered catalog bytes verbatim (>= 8 B) in the port's other data sections (old ports' `k_` tables, literals), and a
column lists source files that spell a VA inside each uncovered row.
- **Found and fixed:** 10 `rl_link_globals.c` tables defined without initializers (2370 non-zero bytes: the zoom-16 / 32
  anim offsets, the feature palette tables, the terrain feature table, the end-movie / armory names, the minimap colours
  …; no pointer slots) — now filled from the exe; 4 old-port initializers that pre-applied a runtime value where retail
  loads 0 (g_bf_591564 = 0x34518632 — create_main_window seeds it; g_tr_selected_button 1, g_tr_p9_bookmark 7,
  g_tr_fl_selected 999 — set by 0x414819 / 0x42150b) — now 0.
- **Now:** 1768 aliased definitions, **0 differ from the exe**. Catalog (83,431 B; 46,586 non-zero): non-zero bytes
  35,252 under a matching aliased definition, 3,035 found under another port name, **8,299 uncovered** (140 rows; 112 of
  them have their VA spelled in `src/` — largest: the CRT's sbh region / iob / ctype / message tables, k_bf_sheet_records,
  the rl_host button tables). 195 VA ranges have two aliased definitions (DUP / NEIGHBOUR blocks).
- **Definitions for the uncovered rows (2026-11-17, `tools/gen_data_defs.py`):** `data_selfcheck.py --nodef-json` dumps the
  catalog bytes no aliased definition covers; the generator turns each run into a definition from the exe (a whole row
  takes its catalog name unless src/ already uses it — then `g_rl_<va>`, as for a partial run; padding at a partial
  run's ends dropped; `char[]` literals for char rows, `uint32_t` / `uint16_t` / `uint8_t` arrays otherwise; `.rdata` →
  const) plus the rhost alias rows: `src/rl_crt_data_globals.c` (41: the CRT's message / assert-file strings, sbh
  region 0, `_iob`, `_ctype`, errtable, tz / mbctype / xcpt tables, init / cfltcvt pointer tables) and
  `src/rl_img_data_globals.c` (123: k_bf_sheet_records, the gfx blit error log, the rl_host button tables, the LZH
  tables, battlefield rule / shot-frame tables, debug literals …). Aliases `rh_rl_{crt,img}_data_globals_alias.h`
  (in rh_hooks_rl_crt.c / rh_hooks_rl_main.c); rhost.exe's .text / .data / .rdata byte-identical (symbols only).
  **Now: 1932 aliased definitions, 0 differ; catalog non-zero bytes 46,559 / 46,586 matched — the 27 left are
  k_pe_debug_directory** (linker metadata, never read; left out on purpose). Ports still read most of these by VA
  (rl_crt.c's G_ macros, rl_host's button tables); the names exist for the image, not yet for the code.
- **The composed image (2026-11-17, `data_selfcheck.py --compose F --strict`, OL2DATA1):** .rdata 0x4d0000..0x4d2000 +
  .data 0x4d2000..0x5cf000 (whole mapped ranges, .bss included) from the port's definitions, zero elsewhere: 4512 + 984,538
  port-defined bytes; differs from what retail_map maps from the exe only in `k_pe_debug_directory` (27 B,
  `COMPOSE_MAY_DIFFER`; `--strict` fails on anything else). rhost `--standalone --data-image F` maps it instead of the
  exe's sections (never read from the file; audit `port_image … fnv` = memory at the seal): **121/121 scenarios = pure
  retail** (wine, `rhost.py standalone --port-data`) — so no game code reads the debug directory either. Gate:
  pre-commit's standalone half runs `--port-data`.
- Pointer slots in the port's copies are raw retail VAs (the generator copies bytes): valid because the standalone
  places data at retail's VAs and thunks every retail code entry — the criterion is byte equality, as checked.
