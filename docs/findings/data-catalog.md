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

## Port names / comments that the catalog contradicts (behaviour equal; rename when touching the file)

| port | says | retail |
|---|---|---|
| src/rl_render.c rl_41608b | workforce category tooltip | screen 0x0c = the trade grid; DAT_0056d8ac = hovered good |
| src/engine_bf_stance.c | DAT_0057a0f0 "MP"; DAT_004d4b18 "the realm's" flag | the standalone skirmish-battle flag; 0x4d4a18..0x4d4b58 indexed by battle DAT_0056d590 |
| src/engine_combat.c k_siege_holds | per castle class, 14 entries | 16 entries per battle |
| src/engine_combat.c k_keep_tbl[12] | — | runs 5 dwords into 0x4d8a58; entries 0..5 meaningful |
| src/engine_combat.c k_muster_cost | supply; county +0x24 provision | county happiness cost (0x4a9a9a); +0x24 = population |
| src/engine_combat.c | 0x4de440 supply-group stock rate | siege-engine build target (as engine_army_state.c) |
| src/engine_npc_schedule.c | NPC job / work schedule (0x49caaa) | AI personality preferences by banner colour (realm +0x07 / +0x08 / +0x09) |
| src/rl_turn_globals.h g_rl_4d8a3c | keep tiers' costs | troops granted to the garrison when a castle completes |
| k_keep_cost[0] | level 0 base | last entry of the 0x4d8990 weapon {wood, iron} table |
| engine_army_* k_facing_march / FACING_MARCH | facing | army +0x1b sub-step counter (0..7); facing is +0x09; tables are 16 entries |
| src/engine_realm.c k_pop_deathmod_tbl | region harshness | county +0x09 health (0..4) |
| 0x4d8990 (three ports) | rate / pool multipliers / AI build cost | {wood, iron} per smithy weapon (0x49ed13) |
| 0x4de79f bytes | period | starting county + county cursor |
| src/engine_army_ai.c realm +0x29 | AI level | game-state-map.md: counties owned (unresolved) |
| G_SBH_HEAPHDR (0x4e4e28) | heap header | field [516] of the static small-block heap header (region pointer) |
| g_rl_4e4520[0x60] | — | a 16 B string slot; retail's copy runs on into errno / _doserrno |
| rl_ctl_globals.h g_rl_4d6a78[5] | — | the shared definition is 32 B |
