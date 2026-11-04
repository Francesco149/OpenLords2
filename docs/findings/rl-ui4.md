# rl ui4 — the 0x401000..0x42ff0f band on retail's layout (2026-11-04)

Partition `ui4` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`), round 3: 99 functions the
corpus reaches whose old ports were not on retail's layout (state structs, ctx structs, file-statics) or not linked in
rhost. All in `src/rl_ui4.c`; rows in `tools/rhost/rh_hooks_rl_ui4.c` (94); the table file is generated from the owner's
row list (`.scratch`-local; the format is the file itself).

## Status (2026-11-04)

All 99 ported. 94 rows, 5 host-level functions without a row (§Host-level). Evidence (§Evidence): every row shadowed
at 0 divergences over the whole corpus with every other rl partition's rows, hybrid PASS; the three DirectDraw-init
functions replace-only `corpus --check` PASS 59/59; the frame guard pair is unverifiable under rhost.

## What is where

| group | functions |
|---|---|
| strutil | 0x401000 find_substr, 0x401085 find_char, 0x4010da bounded memcmp, 0x401136 bounded copy (SRC, DST, n), 0x4011b0 to-lower char, 0x4011ea / 0x401246 case in place, 0x4012a2 strip ctrl, 0x401303 / 0x401333 shifts, 0x4013bc trim, 0x401666 atoi |
| font metrics, localization | 0x4015b9 char width, 0x4014f0 buffer width, 0x401f8f editor width, 0x4036f9 word width; 0x40187a offset, 0x4017bf read (a local walk), 0x4018d7 seek (the walker DAT_005c9270) |
| the text editor (DAT_005cd550, cursor DAT_005bb4a8) | 0x401984 clamp, 0x401a20 filtered key, 0x401d26 insert, 0x401dc8 / 0x401c70 / 0x401c93 delete / backspace / Delete, 0x401ca3 insert toggle, 0x401cbc / 0x401cda / 0x401cfc / 0x401d11 cursor, 0x401f49 length, 0x402009 open, 0x4020ae clear, 0x40210c copy out |
| leaves | 0x4037ee Bresenham setup, 0x40489e / 0x40490d the ten frame-cadence counters, 0x404b2c the single LFSR, 0x404d6b / 0x404d96 / 0x404dc1 / 0x404e03 / 0x404e4b math, 0x404ead / 0x404f4c Manhattan / Chebyshev |
| routers, dialogs, boot | 0x40da1e auto-repeat buttons, 0x40e3ee click hot spots, 0x40e5e7 hover, 0x40ccfa / 0x40e6f2 confirm dialog, 0x40e7e4 the seal, 0x408fcb raw backdrop, 0x40ecc4 hardware defaults (l2.ini / sierra.ini), 0x40e890 / 0x40e8bb the frame guard |
| screens | 0x40f1a0 the screen dispatcher, 0x40f5fd the strategic screen, 0x419c78 the menu bar's chrome, 0x412b33 + 0x412e6b / 0x413155 / 0x413590 / 0x413b30 / 0x4140f3 / 0x414220 the labour screen and its panels, 0x41593b / 0x415bdc / 0x415e42 the rankings, 0x415fb7 the merchant, 0x41789b / 0x417960 diplomacy, 0x4180f6 / 0x4184c6 / 0x418e2d the armoury, 0x410a5d / 0x41ad5d / 0x41aea2 send supplies, 0x41e61d the title backdrop, 0x41fdd6 an option page's list |
| strategic map | 0x429b1d scroll clamp, 0x429ba4 cursor → cell, 0x42a15f scroll pixels, 0x4290d0 / 0x429153 / 0x4291b3 / 0x42925e the category buckets + digest, 0x4298c1 the cell grid, 0x427ed0 the army starts, 0x42847c the merchant prices, 0x428471 (empty) |
| audio, DirectDraw | 0x425b10 DirectSound on, 0x425ded the primary's format, 0x426d11 stream stop, 0x427d19 cue release; 0x42e5c0 DirectDraw on, 0x42ec91 the surfaces, 0x42eb7c the clear, 0x42f1df / 0x42f306 the palette |

## Routes

- **(a) wrap a linked old port** (its result / EAX modelled in the wrapper): strutil 0x401000 / 0x401085 / 0x401136 /
  0x4011ea / 0x401246 / 0x4012a2 / 0x401303 / 0x401333; engine_math 0x404d6b / 0x404d96 / 0x404dc1 / 0x404e03 /
  0x404e4b; engine_path 0x404ead / 0x404f4c (on the mirror g_path_dist_dx / _dy); engine_rng 0x404b2c (pure in / out);
  engine_camera 0x429b1d / 0x429ba4 / 0x42a15f (pure in / out over retail's globals). 20 functions; none diverged.
- **(b) fresh ports** — the other 79: the old port keeps its own state (text_input_state_t, font_metrics_state_t,
  localization_state_t, engine_category_buckets_state_t, ctx structs, engine_labor's file-static task), lives in an
  unlinked module (engine_menu_input / engine / main / config / audio / gfx / game_init / engine_map_init /
  engine_screen_dispatch / engine_screen_strat / engine_rankings / engine_merchant / engine_diplomacy_action /
  engine_sendsupplies / engine_ingame_input), is a file-static (gfx_line.c's bres_setup, strutil.c's memcmp,
  engine_labor's panels), or pins a wrong behaviour in the host tests (below).

## Host-level functions (no row)

| function | why | verification |
|---|---|---|
| 0x40e890 / 0x40e8bb (the frame guard) | they run a frame: rhost's frame boundary is every call of 0x4b99c0 (rhost.c frame_wrap advances the frame counter, the virtual clock, the input queue) — a shadow would run two frames, and a replacement's call of rl_4b99c0 (engine_retail_call to the VA) bypasses frame_wrap | none possible under rhost (ported from the asm for the standalone) |
| 0x42e5c0 (DirectDraw on) | DirectDrawCreate, the window's style / position, a message pump — rhost's DirectDraw objects, window and queue are not retail memory | replace-only `corpus --check --fb-hash --hook replace:` (with 0x42ec91 / 0x42f1df): PASS 59/59, one call each a scenario |
| 0x42ec91 (the surfaces) | CreateSurface / CreateClipper (cf. rl-glue.md's 0x42f148) | same |
| 0x42f1df (the palette) | CreatePalette + Release of the old one | same |

## Rows

HOOK_R wherever the port models retail's EAX on every path (compared) — including the pointer / index EAXes of the
string helpers (0x4011ea / 0x401246 / 0x4012a2: the NUL, 0x401303 / 0x401333: the final pointer, 0x4013bc: the last
index), 0x401984 (MaxCharCount), 0x4020ae (0), 0x4290d0 (5), 0x429153 (16 (b - 1) with c in the low byte),
0x42847c (0xf0), 0x4037ee (the y start), 0x429b1d (128 / 129 - rows), 0x42f306 (255), 0x425ded (22050), 0x415bdc
(the leader). HOOK_V where EAX is a callee's or the caller's on some path (0x401d26, 0x401dc8 / 0x401c70 / 0x401c93,
0x401cbc / 0x401cfc, 0x40e6f2 (0x4b36c0's), 0x427d19, 0x42a15f, the renderers). RH_SH_SAMPLE on 13 hot leaves
(0x401136, the measurers, 0x40187a / 0x4018d7, 0x4037ee, the math / distance helpers); RH_SH_MEDIA on every row that
draws or calls a drawer / router action.

## How the ports reach the rest

- Linked ports on retail's globals, by name: the gfx layer (gfx_panel_*, gfx_sprite_render_*, gfx_wrap_* with the
  walker loaded / stored around each call as in rl_render.c, gfx_text_*, gfx_text_label_*, gfx_line_*),
  engine_hud_menubar_tile_strip / _labels (the dirty state passed: the mirror), engine_map_scene_tick_frame,
  engine_army_ai_set_ranking_dirty / _ranking_dirty (DAT_0056d6a0), engine_territory_dirty_state() (DAT_0053e9ac),
  engine_territory_current_slot() (DAT_0052afe0), g_cell_cursor_col / _row (DAT_00569570 / 78).
- Other partitions by name: rl_47703a / rl_40c928 / rl_42f148 (glue), rl_414819 / rl_41e1dd / rl_414f68 / rl_41515c /
  rl_4152ea / rl_421f14 / rl_422d22 / rl_422ff1 / rl_4233f7 / rl_423241 / rl_4192b1 / rl_419789 / rl_417aef /
  rl_417cef (render), rl_421707 / rl_421d09 / rl_42051c (skirmish), rl_41543f / rl_4154ea (ui2), rl_4b1897 (misc),
  rl_426216 / rl_427990 / rl_4afab9 / rl_4b11ce / rl_4b36c0 / rl_4978ad / rl_497a34 / rl_4b97f9 / rl_4c0580 (io);
  still pending in their round-3 partitions (their stubs run retail): rl_45240a / rl_452160 / rl_4527a6 (ctl),
  rl_46797d / rl_46e1b0 (terr), rl_4af502 / rl_4af8d9 / rl_4b0ab5 / rl_4b1867 / rl_4b18e3 / rl_4b1deb / rl_4b25b0 /
  rl_4b99c0 (host).
- Router actions (a record's +8): engine_retail_call(the pointer) — an entry-patched hooked action runs its row.
- Platform shim (`rl_io_plat.h`), added (`/* ui4 */`): DirectSoundCreate (IAT 0x5cf370) and DirectDrawCreate
  (0x5cf35c) — the standalone loads dsound / ddraw on first use (rhost.exe links neither) —, timeKillEvent (0x5cf5b4),
  UpdateWindow, GetDC, ReleaseDC, GetDeviceCaps, GetPaletteEntries, SetSystemPaletteUse, GetSystemMetrics,
  GetWindowLongA, SetWindowLongA, SetRect, GetMenu, AdjustWindowRectEx, SetWindowPos, SystemParametersInfoA,
  GetWindowRect, PeekMessageA, GetMessageA, TranslateMessage, DispatchMessageA. Pure CRT (strcat, atoi, memset) is the
  host's.

## Seams

- Replaced by rl_<va> (one-line call sites): rl_glue (0x40e3ee, 0x40da1e, 0x40e6f2, 0x402009, 0x401f49, 0x4018d7,
  0x426d11), rl_ui2 (0x40e6f2, 0x402009, 0x401f49, 0x413155), rl_ui3 (0x402009, 0x401f49, 0x426d11, 0x427d19), rl_io
  (0x4298c1, 0x402009, 0x401f49, 0x426d11, 0x427d19, 0x42f306), rl_render (0x401f49, 0x40210c, 0x408fcb, 0x40f5fd,
  0x4298c1, 0x412b33, 0x419c78), rl_misc (0x40f5fd), rl_skirmish (0x408fcb), engine_bf_input (0x40e6f2), engine_bf_setup
  (0x40489e), engine_bf_extern (0x426d11, 0x427d19).
- Not replaced (not an engine_retail_call seam; other owners' bindings): rh_hooks_screens.c / rh_hooks_battle.c call
  0x408fcb, 0x419c78, 0x41e61d, 0x41fdd6, 0x426d11 through retail function pointers (their tables' bindings).
- TODO(rl-link) left in rl_ui4.c (a port exists on a ctx / env / file-static): 0x409397 (the chrome panel + fill),
  0x41a900, 0x40f7d3, 0x41a734, 0x410aa9, 0x410cbd, 0x41e7e1, 0x41f6c7, 0x41f77a, 0x416925, 0x416cf3, 0x417ea7,
  0x418653, 0x418426, 0x4181eb, 0x416308, the county panels 0x4110b1 / 0x41152f / 0x4116fb / 0x411b72, 0x412143,
  0x41b032, 0x41ab67, 0x41dedb, game_init's 0x428007 / 0x428078.
- TODO(rl-extern) (unported, no partition): 0x41e5d0, 0x425a6a, 0x420630, 0x4b2234, 0x4b2730, 0x4b26b8, 0x49792e,
  0x427d72, 0x425d35, 0x42f60e, 0x42f449.

## Globals

`src/rl_ui4_globals.{h,c}` + `tools/rhost/rh_rl_ui4_alias.h`: generated (`gen_skeleton.py --part-globals ui4`,
`dedup_globals.py`: none shared); by hand: the primary buffer's WAVEFORMATEX DAT_00522b90..0x522ba2 folded into one
18-byte block (the generator split it into six scalars; SetFormat takes its address). Other partitions' globals are
declared locally where their header conflicts with rl_render_globals.h (rl_glue_globals.h: DAT_005c9270's type).
For the standalone (exact under rhost, where every symbol is at its VA): g_rl_4dc428 (the menu bar's 3 x 16-B label
table, 0x40c5b0 rewrites it) is generated as one int16; g_bf_542ce0 is 0x18 B but 0x415fb7 reads 0x12d8 B of
mercgrid.pl8 into it; g_rl_4ea180 is 4 B but 0x42eb7c passes it to Blt as a RECT.

## Quirks (retail ground truth)

1. 0x40e3ee (the click router): a HELD kind-2 record (no press / repeat) with DAT_0057d3c8 clear does not advance
   the record pointer — the remaining iterations re-test the same record; the records after it are never tested.
2. 0x40da1e's records are squares (+6 is both sides); 0x40e3ee / 0x40e5e7's are x..x2 / y..y2 rectangles.
3. 0x4017bf / 0x4018d7: an offset past the blob's end (DAT_005aea40) returns that end; 0x4018d7 leaves the walker
   there. 0x4017bf walks a LOCAL pointer (DAT_005c9270 untouched) and ends a string at a NUL after a printable;
   0x4018d7 also at a NUL after a NUL.
4. 0x4020ae (the editor clear) sets the length DAT_0058fe94 to 1, not 0.
5. 0x4298c1: a rotation other than 0 / 2 / 4 / 6 walks six uninitialized locals (no caller passes one: the rotation
   steps by 2 mod 8). Its first loop also computes two mirrored distances it never uses.
6. 0x427ed0 pushes ONE argument to 0x428007 / 0x428078 (they leave the found cell in DAT_00569570 / 78); game_init.c's
   ports take out-params instead.
7. 0x40ecc4: find_substr's 0 is both "absent" and "at offset 0" — a key counts when find_substr + find_char('=') > 0.
   The WindowsDir value is copied 20 bytes into a 40-byte local without a terminator; strip_ctrl stops at the line's
   CR (a 20+ character path would run into stack residue).
8. 0x42f306: IDA's byte_4D4E2E[3 x 255] is DAT_004d512b (inside the 0x4d50f0 block), not the 0x4d4e2e table.
9. 0x42e5c0 saves the system palette with GetPaletteEntries on a NULL HPALETTE (not GetSystemPaletteEntries): it
   fails and DAT_00515d90 keeps what it held. Its work-area clamp of the window rect is dead (the rect is not used).
10. 0x42eb7c's DDBLTFX sets only dwSize and dwFillColor (the rest is stack residue no colour fill reads).
11. 0x419c78: the first ~0x260 bytes are three debug readouts (memory status, time / crc, end / test / max players)
    behind unconditional jumps; the live code starts at 0x419ed4.
12. 0x412e6b: the slot / noun locals are uninitialized for a task outside 5..8; its task-8 arm is unreachable from
    0x412b33 (task 8 is the smithy, 0x413155).
13. 0x415bdc: a tie for the lead goes to the highest realm index (>=); dead realms score 0 but still get a percentage.
14. 0x415e42: rankings 6+ score 2 before 1270, then 6 minus the realm's byte +0x2b.
15. 0x413590: seasons 2 / 3 / other show 3 / 2 / 1 seasons left; winter (4) reads the stock +0x248 instead of +0x2fc.
16. 0x417960 stores the menu-redraw byte 0 then 2 around the empty 0x4b1de0 (a dead store pair; cf. rl-render #5).
17. 0x4b1de0 is an empty function: its calls in 0x40ccfa, 0x412b33, 0x4184c6, 0x417960 are elided.

## Old-port bugs (the host tests pin them; the rl ports are retail's)

- strutil_to_lower_char (0x4011b0): returns 0 outside 'A'..'Z' — retail returns the char unchanged
  (`strutil_to_lower_char_non_uppercase_returns_zero`).
- strutil_decimal_atoi (0x401666): caps the digit run at 8 — retail indexes DAT_004d2030 past its 8 entries
  (`strutil_decimal_atoi_nine_plus_digits_truncates_to_eight`).
- game_init.c's 0x428007 / 0x428078: out-params where retail writes the cursor DAT_00569570 / 78 (a convention, not a
  value bug).

## Evidence

- Batch 1 (commit 59dd3b2, 66 rows): `rhost.py verify --target wine -j 3 --hooks '^rl_'` over the whole corpus (59
  scenarios, 410 rows with every other rl partition's): 410/410 reached, 0 divergences, 0 failed runs, hybrid PASS
  (7264 s); the 66 ui4 rows 6.73 M calls shadowed, 325 M more run by the RH_SH_SAMPLE passes, depth 1..4.
- Batch 2 (28 rows: DirectSound, the palette / clear, the panel renders; the remaining seams): `verify --target wine -j
  3 --hooks '^rl_|^bf_(43ba29|43bd67|477b2f|4778a0)$'` over the whole corpus (59 scenarios, 442 rows — the four bf
  rows are the battlefield ports whose seams now call ui4): 442/442 reached, 0 divergences, 0 failed runs, hybrid PASS
  (7177 s); all 94 ui4 rows 8.04 M calls shadowed + 325 M sampled-out, depth 1..4.
- Host-level replace-only: `corpus --check --target wine --fb-hash --hook replace:rl_42e5c0,replace:rl_42ec91,
  replace:rl_42f1df`: PASS 59/59 (temporary rows, removed).
- Least reached (1–2 scenarios: strat-tour / realm-tour / menus-tour / custom-tour): the labour panels, the rankings,
  the armoury, send supplies, 0x41fdd6, the editor keys (0x401c93, 0x401ca3, 0x401cbc, 0x401cda), 0x401333, 0x4011b0.
