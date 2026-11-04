# rl skirmish — Skirmish mode on retail's layout (2026-11-04)

Partition `skirmish` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`; the gfx layer under it:
`render-under-rhost.md`): the skirmish setup pages (title pages 12 / 13), the battle list (battles.eng) and the
per-battle troop table (troops*.eng, a user .skr), the side setup, the Battle Master ratings (screen 0x2e), the
skirmish masters (0x2f) and score.dat, the two battlefield debug inspectors. 35 functions, 18624 B, all in
`src/rl_skirmish.c`; rows in `tools/rhost/rh_hooks_rl_skirmish.c`.

## Status

All 35 ported, all 35 reached by the corpus (`skirmish-tour`; the two debug inspectors 0x425314 / 0x425487 by
`strat-tour`). `rhost.py verify --target wine -j 3 --hooks '<the 35 rows>'` over the whole corpus (54 scenarios):
**35/35 reached with 0 divergences, 0 failed runs; hybrid PASS** — 13423 calls shadowed (state, and every surface
pixel on the 18 RH_SH_MEDIA rows), 0 skipped. Block coverage and exploration: §Evidence.
With every rl row of master f8fb995 (glue, io, render, misc + these 35): `verify --hooks '^rl_'` over the corpus:
**245/245 reached with 0 divergences, 0 failed runs; hybrid PASS** (all 245 replaced at once, `corpus --check`).

## Layout

- **Globals**: `src/rl_skirmish_globals.{h,c}` + `tools/rhost/rh_rl_skirmish_alias.h`, generated
  (`gen_skeleton.py --part-globals skirmish`), one hand entry: `g_rl_53f5fc[0x44]` — the chosen .skr's file name,
  `byte_53F1E0 + 0x41c` (IDA names it only as an offset into lords2.inf's block, so the generator saw no global).
  Other partitions' globals through their headers (`rl_render_globals.h`: fonts, the localization blob + walker, the
  seal sheet, the palette buffer, `Size`; `rl_io_globals.h`; `engine_bf_input_globals.h`); the glue partition's
  masters table (`g_rl_51fbc0 / 51fd00 / 51fd28`) by local `extern`s (rl_glue_globals.h cannot be included beside
  rl_render_globals.h: their two views of `g_rl_5c9270`). The save block (`s_slot_*`, the player / aux / territory
  records), `g_gfx_*`, `g_combat_*` through their owners.
- **Mirrors**: the below-status redraw DAT_0053e9ac through `engine_territory_dirty_state()`; DAT_005533e0[2] IS
  DAT_005533e8 (engine_army_state's `g_army_state_index`, a mirror) — `ready(p)` routes index 2 there.
- **Records**: the battle troop table DAT_00516ac0 = 55 battles × 220 B = [side 0..1: 110 B][tier 0..4: 22 B]
  [type 0..10: WORD] — IDA's ten names word_516AC0 .. word_516B86 are the (side, tier) rows (`TR16(b, side, tier,
  k)`); battles 0..34 built in (set base DAT_004d49b8 = 0 / 10 / 20 / 35), 35..54 the .skr's. The two armies live in
  territory records 1 / 2 (the human's DAT_00553efc, the computer's DAT_00553f34), 3 / 4 a backup, 5..8 the copies
  the ratings screen compares (5 / 6 after the battle, 7 / 8 before).
- **Drawing**: the gfx ports by name (as rl_render.c: the localization walker and the caret's editor are loaded from
  retail's globals around each call; the localization seek 0x4018d7 is the linked `localization_seek_skip` on that
  state). Every draw row carries RH_SH_MEDIA.
- **score.dat's first field** is 0xa0 B from DAT_0051fd00, i.e. three port symbols (quirk 2): read / written through
  a buffer (`span_put` / `span_get`) so the standalone, where they are not contiguous, gets the same bytes.

## Rows

HOOK_V everywhere but 0x42c4e2 (HOOK_R: the custom-armies page 0x43d6c3 / 0x43d882 tests it). Every other caller
drops EAX (checked at every call site in all_ida.asm).

## Seams left

- TODO(rl-link): io_path_chdir_base 0x4af502, io_read_file_at 0x4af8d9, gfx_load_background_image 0x408fcb,
  gfx_load_panel_sheet 0x4b25b0, the palette set 0x4b0ab5, 0x409397 (the chrome panel box), the battlefield minimap
  strip 0x4bc1d1, gfx_load_system_sheet_select 0x499a1c.
- By name across partitions: rl_4af70c / rl_414e06 / rl_498dcb / rl_43f24b (ui2), rl_4aecf0 / rl_4af767 (io),
  rl_4360f2 / rl_43eda0 (glue), bf_47aaa3 / bf_47c4ba / bf_47b8b2 (bf setup).
- 0x46ead0 is `memset(rec, 0, 0x1a4)`: inlined (as engine_army_state / game_init do).
- Callers elsewhere still reach this partition through `engine_retail_call`: rl_glue.c 0x42c64f (the siege result),
  rl_io.c 0x42b780 — they can call `rl_42c64f` / `rl_42b780` by name now.

## Quirks (retail ground truth)

1. 0x42cc4a: only the HIGHER of the two new ratings is looked up in the masters table (same rating under that
   side's name → return): a hit skips everything — the other side's entry and the human's best-ten entry too.
2. 0x42d216 / 0x42d2dc: score.dat's first field is 0xa0 B from DAT_0051fd00: the 10 masters' ratings, the human's 10
   best (DAT_0051fd28) and the first 0x50 B of battles.eng's text DAT_0051fd50. Reading score.dat overwrites the
   start of the battle list's text; writing it saves those 80 bytes — the shipped score.dat holds "Three
   Bridges\0\0A Bridge Too Far\0\0You may be sure ..." at 0x50.
3. 0x42cc4a: a master's name is a 32-byte copy of the side's 44-byte aux record from +4 — the 16-byte name and 16
   bytes of the fields after it (the shipped score.dat: "aaaaaa … castles").
4. 0x42c64f: an advantage-weighted winner's bonus is computed (300 × (100 − 10 × DAT_0051fae0[battle]) % and its
   complement, by attacker) and then overwritten by 500 for both: dead code. DAT_00553f3c set = the defender won;
   a surrender (DAT_0056d5c8, by DAT_005656f8) rates the side that gave up 0 and gives no bonus.
5. 0x42ac0c: troops*.eng carries five tiers per side; every tier is then overwritten by tier 2 and the difficulty
   spread re-derived (tiers 0 / 1 +16 % / +8 %, 3 / 4 −8 % / −16 % of tier 2, first 7 types only; engines 7..10
   capped at 9). The .skr battles (0x42d4aa) get ±c/3 / ±2c/3 on all 11 types, uncapped.
6. 0x42ab15: the string walk's first step reads the byte below the text (DAT_0051fd4f, the best ratings' last
   byte) — a text starting with a NUL counts a string there when that byte is non-zero.
7. 0x42c4e2: type 10 only for the attacker, 7..9 only for the defender, on a siege only.
8. DAT_005533e0[p] (a side's MP ready flag, p = 1 / 2): [2] is DAT_005533e8, the army-state index.
9. 0x42051c / 0x42130f: their arguments are unused (callers pass 1 / 6, 162).
10. 0x425314 / 0x42150b: positions kept in locals stored before the early return (dead stack writes; residue).

## Evidence

`verify` (above), per row (calls / shadow depth): 420147 2314/1 · 42051c 74/1 · 4207c3 75/2 · 4209c1 36/2 · 420d40
2314/1 · 420de4 38/2 · 421005 75/2 · 421231 37/2 · 42130f 38/2 · 42150b 1/1 · 421707 318/1 · 421d09 198/1 · 425314
80/1 · 425487 80/1 · 42aa90 4/2 · 42ab15 484/3 · 42ab82 40/3 · 42ac0c 4/1 · 42b780 3/1 · 42b7f7 1/1 · 42b919 1/1 ·
42b9c4 20/1 · 42ba40 27/1 · 42bf46 34/1 · 42c476 3986/1 · 42c4e2 3119/1 · 42c5ad 3/1 · 42c5fe 3/2 · 42c64f 3/1 ·
42cc4a 3/2 · 42d216 3/2 · 42d2dc 3/2 · 42d389 1/2 · 42d4aa 2/1 · 42d857 1/2.

**Block coverage** (rhost `--coverage` on every basic-block start of the 35 functions — entry, jump targets, the
instruction after a conditional jump: 513 blocks; retail-only): **417 / 513** over skirmish-tour + strat-tour.
Not reached by the corpus: every MP arm (0x420147's draw, 0x42150b's ready colours, 0x42ba40's, 0x42ac0c's
troops.eng, 0x42b780's page 11); the human as side 2 (0x4209c1 / 0x420de4 / 0x42b7f7); no .skr / no battles.eng /
no troops file / no score.dat; a list row past the count (0x421005); the advantage clamps and the engines' cap of
0x42ac0c (the shipped files never trip them); 0x42c4e2's engines (a1 >= 7: the custom page never offers them in the
corpus); 0x42c64f's surrender and DAT_00553f3c arms and its human-defender dead code; 0x42cc4a's ladder below 51 and
above 75 games (the shipped score.dat has 55) and the duplicate-rating return; 0x42d389's missing-.skr fallback;
0x425314 / 0x425487's draw (the debug flag).

**Exploration probes** (rhbot Session: replay skirmish-tour with rows in shadow, poke retail memory, step — poked
state, NOT a pure replay): DAT_0055402c = 1 with DAT_00568960 = 1 then 2 on the setup page (sub-state 2): the two
inspectors' draw paths, 40 calls each, 0 divergences (state + pixels); one frame with DAT_00553030 = 1 on the setup
page: 0x420147's chat line (a1 != 0 arm), 0 divergences. The siege engines of 0x42c4e2 stayed unreached (DAT_0053e91c
= 2 poked before Cust.: the page's engine buttons still do not call it).
