# Retail-layout port, partition `glue` (2026-11-04)

`src/rl_glue.c` (globals `src/rl_glue_globals.{h,c}`, generated + widened by hand), rows
`tools/rhost/rh_hooks_rl_glue.c`; design `battlefield-sim-port.md` (family `rl`). The UI's logic glue: the
strategic screen's router-table actions, the in-game dispatcher's sub-handlers (FUN_0042ff10), the frame
driver's pre-tick steps (FUN_004b99c0), the battle prompt / siege preparation, the battlefield casualty
write-back, the MP message sender. All 76 functions ported; 74 have rows.

## What is where

| group | functions |
|---|---|
| in-game input sub-handlers (EAX read: HOOK_R) | FUN_0040deca / FUN_0040dd92 (menu bar), FUN_004323fe → FUN_0043253a / FUN_00432443 (minimap), FUN_0043292d / FUN_004329a4 (HUD hotspots), FUN_00437002 (army panel), FUN_00438990 (county panel), FUN_00438a91 (battle prompt buttons), FUN_00439079 (route overlay), FUN_00439122 (tax slider), FUN_004393eb (workforce drag), FUN_0043b4cb (diplomacy county pick), FUN_004369bd (portrait pick) |
| router-table actions (entry-patched, HOOK_V) | FUN_00433e93 / FUN_00433f49 (save / load), FUN_004344f2 (quit), FUN_0043470d, FUN_00434cbe / FUN_00434cee (volume sliders → FUN_0040e761), FUN_0043496c / FUN_00434988, FUN_0043618b / FUN_0043631e (diplomacy modes), FUN_00436872 (alliance answer), FUN_00436b22 / FUN_00436b59 (castle), FUN_0043733a / FUN_0043744f (disband + confirm callback), FUN_004374c4 (transfer), FUN_004376bb (attack → muster), FUN_00438acc, FUN_0043b593 (take the field / auto-resolve), FUN_0043b681 / FUN_0043b7c4 (siege engines), FUN_0043b848 / FUN_0043b98a (lift / assault) |
| pre-tick steps | FUN_004782c5 (result-panel timeout), FUN_00472a31 (hovered army), FUN_00498458 (gfx check), FUN_00448d7e (county notice), FUN_0047703a (tooltip down), FUN_00440666 (MP poll, HOOK_R), FUN_0049b8b0 (MP turn vote), FUN_00481b9a (battlefield census), FUN_00482111 (projectile census), FUN_0041423f (empty), FUN_0042e26f (movie playing) |
| other | FUN_0047f474 (casualty write-back), FUN_0043eda0 (MP send), FUN_0040ae12 / FUN_00475d73 (.pl8 sprite load, portrait), FUN_0042a8d1 / FUN_0042a9ab (tile drawers), FUN_0042d179 (high-score defaults), FUN_0042f148 (DD surface), FUN_004b1310 (grey-out), FUN_00409429 (panel frame), FUN_0040f10f (speed probe), FUN_00475b41 (end movie), FUN_0046c4a3 (cell box redraw), FUN_00451f8a (full repaint), FUN_00472986, FUN_00433a70, FUN_004330c6, FUN_0040e00a / FUN_0040e099 / FUN_0040c8d1 / FUN_0040c928 / FUN_0040dfcd, FUN_004360f2, FUN_00432893, FUN_0043933b |

## Seams

- Retail clocks: `clock_ms()` / `ticks_ms()` — rhost binds retail's imports (timeGetTime DAT_005cf5c4, GetTickCount
  DAT_005cf3d8: the virtual clock) from a constructor in the table file (`rl_glue_bind_clock`); Sleep is
  `bf_sleep`.
- Retail calls kept (`engine_retail_call`): TODO(rl-link) — a port exists on a port struct / unlinked module:
  0x40e3ee / 0x40da1e (engine_menu_input.c), 0x40e6f2 (engine_confirm_dialog.c: retail keeps the callback VA),
  0x472bc5 (engine_event_queue.c), 0x402009 / 0x401f49 (text_input.c), 0x4b3f0a / 0x4b3ec0
  (engine_workforce_hud.c), 0x4050c0, 0x476768, 0x4b1deb, 0x4984dc, 0x4b18e3, 0x41b032, 0x452160 (engine.c & co.),
  0x477249 (engine_tooltip.c), 0x4af8d9 (io_read_file_at), 0x4af502, 0x4018d7 (localization.c: a port state),
  0x409934 / 0x409c93 (gfx_panel.c: linked on retail's layout since 7965fc7, but they take the chrome sheet
  DAT_0057d3d0, no port symbol yet), 0x4a8426,
  0x45337b / 0x4538f6 / 0x463c06 / 0x463c62 (map terrain drawers), and the event-queue takers
  0x437535 / 0x43b622 / 0x4a8aab / 0x4ab383 (engine_combat.c: `evq` argument). TODO(rl-extern) — unported:
  0x40df62, 0x40cd58, 0x426d11, 0x42c64f, 0x4788c6, 0x4b209a, 0x4608de / 0x46079d, the MP layer
  (0x440dd2, 0x43f8ff, 0x43fd2a, 0x43f5c5, 0x4407e5, 0x49b973, 0x43e668, 0x440038, 0x4b90fb, the payload builders
  DAT_004d57f0[]).
- Other partitions by name: bf_43c634, bf_4778a0, bf_4784ca, bf_47f76d, bf_480f8b, bf_4987b7; rl_4aecf0,
  rl_4b3940, rl_42d91b, rl_499aca, rl_427990, rl_4afab9 (io).

## Quirks (retail behaviour a naive port misses)

1. **FUN_0043eda0 in single player returns the caller's EAX** (it only tests DAT_00553030). Callers that "return"
   it (FUN_00436872, FUN_0043b593 …) are router actions whose EAX nobody reads: HOOK_V, the port returns 0.
2. **FUN_00433a70's default case returns the caller's EAX** (the only caller passes 0..11).
3. **FUN_00440666 calls FUN_0043fd2a with no argument pushed** (IDA invents `v1 = sub_43F8FF()`): the callee reads
   the caller's saved EDI. MP only; the port passes 0x43f8ff's EAX (unmodelled, not reachable in the corpus).
4. **FUN_0047f474 tests DAT_0053e8fc and ignores the result** (`cmp; jz $+6`).
5. **FUN_0043253a reads IDA's `byte_52AE10[128 (y − 25) + x]` with x ≥ 480**: that is the minimap region grid
   DAT_0052aff0 at [128 (y − 25) + x − 480].
6. **FUN_0040dd92 runs the pull-down item through the menu record's own table** (record +8 → 12-B items, the
   action at +4) after restoring the screen — the items are router actions in this partition (save / load /
   options …), reached nested.
7. **FUN_0043b7c4 adds the whole dword argument to an int16** (IDA types it `__int16`): the same low half.
8. **FUN_004330c6's EAX** is the colour byte over 44 × local's upper bytes (char); nobody reads it.
9. **Retail indexes past IDA's next name** in several tables the generator sized by the next xref: DAT_0057cb40
   (picks 1..5), DAT_004d5508 (+0x40 colours), DAT_0053f6a0 (6 × 128-B muster rows; DAT_0053f710 is row 7),
   the hotspot tables DAT_004dc4d0..0x4dc5a8 and DAT_004dd640 (14 × 24 B), the palette DAT_004ea1b0 (768 B),
   DAT_0051fd00 / DAT_0051fbc0. Widened by hand in rl_glue_globals (exact under rhost either way; the
   standalone's storage needed it).
10. **FUN_00475d73 reads realm record +5 / +7 for p ≤ 0 and p ≥ 6** before overriding the sprite (16 / 17): the
    port tests p first (same result, no read past the table).

## rhost limits hit (shared-file needs)

- **rhost.c `redirect_calls` is a raw byte scan** (any `E8 rel32` aimed at the VA): for 0x47f474 it finds a false
  site at 0x47f421, inside FUN_0047efee's `mov dword_4EE8E8, 0` (C7 05 E8 E8 4E 00 …: E8 4E 00 00 00 → 0x47f474),
  rewrites it, and retail crashes at 0x47f41e (write f1aae8e8) when a battle starts — in every shadow pass,
  even with the hook never called. rl_47f474 has no row (its port runs inside rl_4782c5's shadow). Fix: take the
  call sites from the IDA call list, or decode instructions.
- **IDirectDraw::CreateSurface (FUN_0042f148) is not rolled back by the shadow**: retail's run made surface 2,
  the port's 3 (`ret r00000002/p00000003`, the diff at 0x20005b20 in rhost's DDraw state). Replace mode passed
  `corpus --check` (38 scenarios, the hybrid pass of 2026-11-04 11:06); no row.

## Evidence

`rhost.py verify --target wine -j 3 --hooks '^rl_'` over the whole corpus (51 scenarios, 2026-11-04 11:12–11:37,
before the master merge of 7965fc7): **74/74 reached with 0 divergences, 0 never called; hybrid PASS** (all 74
replaced, `corpus --check`: every state hash and presented frame). 14.34 M shadowed calls; rl_4782c5 skipped 10
calls whose retail run grew the heap arena (rh_state.c: "arena commit changed", not compared). Re-run after
merging master 0c33e3b (the gfx layer, the whole battlefield port; 11:45–12:10): the same, 74/74, 0 divergences,
hybrid PASS. Never-reached
paths (ported from the asm, unverified): the MP arms (every `MP_ON` branch, FUN_0043eda0's send, FUN_00440666's
link read, FUN_0049b8b0's vote), FUN_0040dd92's no-item close, FUN_004b1310 with a null buffer.
