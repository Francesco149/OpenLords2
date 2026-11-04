# rl ui4 — the 0x401000..0x42ff0f band on retail's layout (2026-11-04)

Partition `ui4` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`), round 3: 99 functions the
corpus reaches whose old ports (strutil.c, text_input.c, font_metrics.c, localization.c, engine_math.c, engine_path.c,
engine_camera.c, engine_category_buckets.c, engine_rng.c — linked; engine_menu_input.c, engine.c, main.c, config.c,
audio.c, gfx.c, game_init.c, engine_map_init.c, engine_screen_dispatch.c, engine_screen_strat.c, engine_labor.c,
engine_rankings.c, engine_merchant.c, engine_diplomacy_action.c, engine_armory.c, engine_sendsupplies.c,
engine_title_render.c — unlinked or ctx-based) were not on retail's layout. All in `src/rl_ui4.c`; rows in
`tools/rhost/rh_hooks_rl_ui4.c`.

## Status

Batch 1 (2026-11-04 22:12): 66 of 99 ported with rows — strutil, font metrics, localization, the text editor, the line
setup, the frame cadence, the single LFSR, the math / distance helpers, the routers, the confirm dialog, the hardware
defaults, the screen dispatcher + strategic screen, the camera, the category buckets, the map cells, the army starts,
the merchant defaults — plus the two host-level frame functions (no row). `rhost.py verify --target wine -j 3 --hooks
'^rl_'` over the whole corpus (59 scenarios, with every other rl partition's rows: 410 rows): 410/410 reached, 0
divergences, 0 failed runs, hybrid PASS; the 66 ui4 rows: 6.73 M calls shadowed (state compared, RH_SH_MEDIA rows also
every surface pixel), 325 M more run by the sampled-out passes of the 13 RH_SH_SAMPLE leaves; depth 1..4.

## Routes

- **(a) wrap a linked old port** (its result / EAX modelled in the wrapper): strutil 0x401000 / 0x401085 / 0x401136 /
  0x4011ea / 0x401246 / 0x4012a2 / 0x401303 / 0x401333; engine_math 0x404d6b / 0x404d96 / 0x404dc1 / 0x404e03 /
  0x404e4b; engine_path 0x404ead / 0x404f4c (on the mirror g_path_dist_dx / _dy); engine_rng 0x404b2c (pure in / out);
  engine_camera 0x429b1d / 0x429ba4 / 0x42a15f (pure in / out over retail's globals).
- **(b) fresh ports** — everything else: the old port keeps its own state (text_input_state_t, font_metrics_state_t,
  localization_state_t, engine_category_buckets_state_t, ctx structs, file-statics), lives in an unlinked module, is a
  file-static (gfx_line.c's bres_setup, strutil.c's bounded memcmp), or pins a wrong behaviour in the host tests
  (strutil_to_lower_char returns 0 outside 'A'..'Z'; strutil_decimal_atoi caps at 8 digits — retail does neither).

## Host-level functions (no row)

| function | why |
|---|---|
| 0x40e890 / 0x40e8bb (the frame guard) | they run a frame: rhost's frame boundary is every call of 0x4b99c0 (rhost.c frame_wrap advances the frame counter, the virtual clock, the input queue) — a shadow would run two frames; a replacement's call of rl_4b99c0 (engine_retail_call to the VA) bypasses frame_wrap. Unverifiable under rhost; ported for the standalone. |

## Quirks (retail ground truth)

1. 0x40e3ee (the click router): a HELD kind-2 record (no press / repeat) with DAT_0057d3c8 clear does not advance
   the record pointer — the remaining iterations re-test the same record, so the records after it are never tested.
2. 0x4011b0 returns its argument unchanged outside 'A'..'Z' (the old port returned 0); 0x401666 multiplies a 9th+
   digit by whatever lies past DAT_004d2030's 8 powers of ten (the old port capped at 8).
3. 0x4017bf / 0x4018d7: an offset past the blob's end (DAT_005aea40) returns that end; 0x4018d7 leaves the walker
   there. 0x4017bf walks a LOCAL pointer (DAT_005c9270 untouched).
4. 0x4020ae (the editor clear) sets the length DAT_0058fe94 to 1, not 0.
5. 0x4298c1: a rotation other than 0 / 2 / 4 / 6 walks six uninitialized locals (no caller passes one: the rotation
   steps by 2 mod 8).
6. 0x427ed0 pushes ONE argument to 0x428007 / 0x428078 (they leave the found cell in the cursor DAT_00569570 / 78);
   game_init.c's ports take out-params instead.
7. 0x40ecc4: find_substr's 0 is both "absent" and "at offset 0" — a key counts as found when find_substr +
   find_char('=') > 0. The WindowsDir copy is 20 bytes into a 40-byte local without a terminator; strip_ctrl stops at
   the line's CR (a 20+ character path would run into stack residue).
8. 0x42f306: IDA's byte_4D4E2E[3 x 255] is DAT_004d512b (inside the 0x4d50f0 block), not the 0x4d4e2e table.
