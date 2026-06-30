# Army-select / move click path — port plan

> 2026-07-01. Picks up session 12's re-scoped "0x11 popup" lead
> ([[project_event_popup_arms]]): session 12 estimated "5 unported callees,
> ~3.1 KB" for `FUN_0043ce1a`. Full archaeology this session found that
> estimate was based on an incomplete grep — most of the *direct* callees were
> already ported, but the *true* full closure (incl. the screen-0x10
> move/merge-confirm chain that actually produces the 0x11 popup) is bigger:
> **32 unported functions, ~9.3 KB**. Every one of them is small (max 1263 B)
> and self-contained once its own deps are known — no remaining unknowns as
> of this writing. This doc is the call graph + tier order so the work is
> resumable if interrupted mid-session.

## Three hook slots, one interactive subsystem

`engine_ingame_input.h`'s `engine_ingame_hooks_t` already declares two NULL
hook slots this frontier fills:
- `army_select` → `FUN_0043ce1a` (map LMB on screen 0 — select/reassign an
  owned army, or initiate a move/merge onto another cell)
- `edge_scroll` → `FUN_00432221` (cursor-at-edge map scroll; fires on screen 0
  AND screen 0x10)

Plus a **new dispatcher case** the skeleton doesn't have yet: screen `0x10`
(entered when `FUN_0043723a` — reached via `army_select`'s "re-click same
army" path — sets `DAT_004eac50 = 0x10` with the "ready" flag
`DAT_0059151c` set). The master dispatcher's screen==0x10 case (`42ff10.c`
lines ~553-572) calls `edge_scroll` first, then gates on
`DAT_004eafb4 != 0 && DAT_00553ecc < 1` (a fresh LMB press, no popup open)
before calling `FUN_004a9252` — the real "what did the player just click as a
move/merge target" resolver, which is where the 0x11 popup
(`FUN_004a9372`, code 0xa6) actually lives.

So: **one click selects an army (army_select, screen 0→0x10), a second click
resolves the destination (FUN_004a9252, screen 0x10)** — a two-click
select-then-target flow, not a drag gesture.

## Tier order (bottom-up; each tier's deps are fully in the tier above or
already-ported)

**Tier 0 — pure leaves, no unported deps:**
| fn | bytes | what |
|---|---|---|
| `FUN_00448648` | 97 | notify every army of `owner` matching a class via ported `FUN_0044f318` (plan_resource) |
| `FUN_004b3f0a` | 74 | trivial strided blit-copy loop |
| `FUN_004517ca` | 107 | table lookup (workforce slot → sprite-row index), 1 special-case |
| `FUN_004519ca` | 147 | fill icon-grid row from a threshold table |
| `FUN_00451a5d` | 349 | fill icon-grid row, two-sided variant |
| `FUN_004126c9` | 204 | render one icon-grid row via already-ported `FUN_0040a21a` |
| `FUN_004a8426` | 148 | territory-linkage byte adjuster (`+0x199` sanity check) |
| `FUN_004b3e51` | 111 | memset-style zero-fill helper |
| `FUN_0046c6ba` | 322 | clear a bit (0x40) across an 8×0x40 overlay grid |
| `FUN_004bbbe3` | 157 | edge-scroll rate-limit gate (timeGetTime-based, turbo-clock-safe) |

**Tier 1:**
| fn | bytes | deps |
|---|---|---|
| `FUN_0045161e` | 428 | 4517ca + 4518a5 |
| `FUN_004518a5` | 293 | 4517ca + 4519ca/451a5d |
| `FUN_00412666` | 99 | 4126c9 |
| `FUN_0046f68e` | 114 | 4b3e51 + 46c6ba + ported (4708d0/46f700) |
| `FUN_004997bf` | 151 | malloc-or-exit buffer-ensure boilerplate (standalone) |
| `FUN_0040ccfa` | 94 | all-ported (4b1de0/4093e0/402d37) |
| `FUN_004b36c0` | 84 | **audio seam** — plays `S010_XX.wav`; bind NULL like other SFX seams |
| `FUN_004a98fe` | 412 | all-ported (46ff43/46f700/4701ac/4707be/43b917) — the SP merge-EXECUTE primitive, shared by 4 callers below |

**Tier 2:**
| fn | bytes | deps |
|---|---|---|
| `FUN_00412143` | 734 | 4997bf, 4b3f0a, 45161e, 412666 + ported (4050c0/4af502/4af8d9/40a682/402d37/40a21a/40d1bc) — renders a per-slot workforce icon grid (`villani1/villani2/vill.pl8` sheets) for the selected army's home county |
| `FUN_004a8d9c` | 111 | 46f68e |
| `FUN_0040e6f2` | 111 | 40ccfa, 45240a (ported), 4b36c0 — generic Yes/No confirm-dialog opener, callback = param 4 |

**Tier 3:**
| fn | bytes | deps |
|---|---|---|
| `FUN_0043723a` | 245 | 4a8d9c — sets screen=0x10 (the army_select → move-target handoff) |
| `FUN_0043d309` | 675 | 448648 + ported (4502ca/4485a5/44f6e7/44df5f/44edc2/472bc5) — **worker-task toggle**, NOT "issue move order" as session-12 guessed |

**Tier 4:**
| fn | bytes | deps |
|---|---|---|
| `FUN_004371ce` | 108 | 43723a — confirm-or-direct dispatch into the screen-0x10 handoff |
| `FUN_00431f59` | 712 | 4bbbe3 — the actual edge-scroll (8-way scroll by direction code) |
| `FUN_00432221` | 477 | 431f59 — **`edge_scroll` hook body**: classifies cursor-near-edge into an 8-way code, calls 431f59 |

**Tier 5 — the two top-level entry points:**
| fn | bytes | deps |
|---|---|---|
| `FUN_0043ce1a` | 1263 | 43d309, 412143, 4a8426, 4371ce + ported (429ba4/46d5fe/41b032/43278b/4050c0/472bc5) — **`army_select` hook body** |
| `FUN_004a9252` | 288 | 40e6f2 + 4a983b/4a960f/4a975d/4a9372 (siblings below) — screen-0x10 click resolver |

**Tier 5 siblings (`FUN_004a9252`'s 4 confirm-dialog callbacks — same shape,
each posts a notice or calls 4a98fe to execute):**
| fn | bytes | event codes posted | notes |
|---|---|---|---|
| `FUN_004a983b` | 195 | 0x11e | direct path (no confirm), garrison-occupied check |
| `FUN_004a960f` | 334 | 0x11d / 0x11c | empty-cell / new-county-claim |
| `FUN_004a975d` | 222 | 0x112 | merge headcount cap check (`0x5dd`) |
| `FUN_004a9372` | 669 | **0xa6 = the 0x11 popup**, also 0xa4/0xa5/0xa7/0x11b/0x121 | garrison-merge-cap notice + 4 siblings sharing the same template |

All 5 of these follow the identical shape: `if (DAT_0059151c) { ... if
(g_sim_mp_active == 0) FUN_004a98fe(...) /* SP: execute */ else
FUN_0043eda0(code,0) /* MP: no-op in SP build */ }`. **`FUN_0043eda0`
(637 B — NOT what it looks like; see below) never needs porting for this
frontier** because `g_sim_mp_active` (retail `DAT_00553030`) is 0 in SP,
which is the only build target ([[project_drop_in_defaults]]).

## Key corrections vs. session-12's framing

1. **`FUN_0043278b` was already ported** (`engine_territory_recenter_on_tile`,
   commit `1f9b22f`, landed *before* session 12 even though session 12's note
   listed it as one of "5 unported callees" — a grep miss, not a regression).
2. **`FUN_0043d309` is a worker-task toggle, not "issue move order."** The
   actual move EXECUTOR (`engine_army_state_run`/`engine_army_move_run`,
   already ported) is reached a completely different way (the per-tick
   simulation, not this click handler). This handler just flips a task-slot
   bit and re-plans economy — almost entirely glue over already-ported
   primitives.
3. **`FUN_00412143` is a RENDER function** ("workforce icon grid for the
   selected county"), not "hover/select cursor feedback" as guessed.
4. **`FUN_0043eda0` is NOT a generic "open screen N" dispatcher** — it's a
   637 B input-replay-log + jump-table command dispatcher
   (`PTR_FUN_004d57f0[code]`, ring buffer `DAT_00512fb0`, used for
   multiplayer command propagation). The existing `engine_ranking.c` mention
   (`engine_ranking_bind_game_over_fn`) is a NULL-default SEAM, not a real
   port of this body. It's irrelevant here because every call site in this
   frontier is `if (g_sim_mp_active) FUN_0043eda0(...)` — dead in SP.
5. **The 0x11 popup is reached via a two-click flow** (`army_select` sets up
   screen 0x10 on a re-click of an already-selected army; a second click
   resolves the target via `FUN_004a9252`), not directly from `FUN_0043ce1a`.

## Field notes (g_per_army_768, `ARMY_REC(key) = &g_per_army_768[key*0x300]`)

Already named (engine_realm.c): `+0x1c0` keep level, `+0x1c1` snapshot level,
`+0x1c3` keep-level gate, `+0x05` owner, `+0x16` realm-tally contribution.
Newly touched by this frontier (name as ported, mirroring the existing
comment style):
- `+0x1bc` (i32) — a territory KEY the army is targeting/linked to (read by
  `4a8426`/`4a9372`/`4a983b` as "the cell this army is merging into")
- `+0x1c2` (byte) — gate byte read by `4a9372` (the "ONE new unnamed byte"
  session 12 flagged); behavior: `fb72==0` selects the capacity-check branch
  vs. the `0xa5` immediate-reject branch — name something like
  `merge_target_pending` pending final confirmation during port
- `+0x70` (i32, `DAT_0053fa20`) — already implicated in `43ce1a` as a
  "has a destination order" pointer/flag (non-zero gates the recenter +
  screen transitions)
- `+0x1bb` (byte, `DAT_0053fb60`) — garrison-toggle flag (`43d309`'s
  `param_2==4` branch)
- task-slot bytes `+0x18*slot` off `DAT_0053fc47` base (`43d309`'s else
  branch) — 5ish task slots, no existing name
- `+0xc8`-ish row `DAT_0053fa68/74/78/7c` (workforce render, `45161e`) —
  8 entries × 0xc stride, looks like per-slot (low,mid,high?) production
  bounds feeding the icon-grid fill — needs the render-parity pass to pin
  down exact semantics

Territory record (`TERR_REC(idx) = &g_per_territory_420[idx*0x1a4]`):
`+0x00` owner/type, `+0x08` kind (1=county? 3=placed army, matches
`engine_territory_place_entity`'s `kind`/`type` params), `+0x199` already
named "besieged-region byte" (engine_combat.h:410) — `4a8426`/`43ce1a` read
it as a coarser "is this army-linked" gate; needs cross-check against the
combat.h doc when porting (may be the same byte serving both roles, or two
different territory-record interpretations — check before assuming alias).

## Port order for this session

1. Tier 0 (10 fns) — mechanical, host-test each.
2. Tier 1 (8 fns) — small assembly, `FUN_004b36c0` bound as a NULL audio seam.
3. Tier 2 (3 fns).
4. Tier 3-4 (5 fns) → **wire `army_select` + `edge_scroll` hooks in engine.c**,
   commit as a working unit (this alone resolves session 12's primary ask).
5. Tier 5 (6 fns) → add the screen-0x10 dispatcher case, wire
   `FUN_004a9252`'s call site, commit.
6. Port the `g_popup_type == 0x11` render arm in `engine_popup_scroll_render`
   (mirrors 0x10/0x12's existing arms) + verify via
   `--force-popup 0x11:0xa6:KEY`.

Verification: host tests throughout (this is almost entirely pure
logic/table-driven, like the 0x12 arm's producer was). The render piece
(`FUN_00412143`'s icon grid + the 0x11 popup body) wants the usual
zoom-diff + `--force-popup` visual pass before calling it done — defer that
to the end, after the logic is host-test-green, per
[[feedback_zoom_dont_sample]] / [[feedback_show_render_as_diff]].
