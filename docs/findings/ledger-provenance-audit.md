# Ledger provenance pollution — the FUN_-token gotcha

How the derived port ledger over-counts, why it's hard to fix
automatically, and what was corrected on 2026-05-29.

## The mechanism

`tools/gen_port_ledger.py` decides a function's status from one signal:
does its `FUN_<va>` token appear anywhere under `src/`?

| ref location | status assigned |
|---|---|
| `FUN_<va>` in a file whose module has a `tests/test_<stem>.c` | **tested** |
| `FUN_<va>` in a src file with no host test | **ported** |
| no `FUN_<va>` anywhere in src | unported |

This rests entirely on the convention that the token is written **only**
in a port's provenance comment. It breaks the moment a ported module's
comment names an *unported* neighbour by its `FUN_<va>` token:

- a TODO / survey list — `/* TODO(sim): FUN_0049a010 (982 B), … */`
- a "describing retail" note — `retail calls FUN_0043f4b7 which …`
- a call-chain diagram — `FUN_004b2922 → FUN_004b284e → …`
- a call-pattern — `local_c = FUN_00440666()`
- a size-annotated dependency list — `FUN_00485bb1 (1592 B), …`

Any one stray token silently promotes the neighbour to **touched**, and
to **tested** if the host file has a test. It inflates both the function
count and — worse — the **code-byte** headline, because one big
unimplemented function (e.g. `0x47309e`, 10 915 B, named only as an
unported callee in `engine_event_queue.h`) carries a lot of bytes.

This is the recurring trap flagged in the project memory
(`project_ledger_fun_token`). The 2026-05-29 roster dispatch hit one
instance; this audit found it was systemic.

## Why it can't be auto-fixed in the generator

There is **no reliable syntactic separator** between provenance and
pollution, because both are written many ways in this tree:

**Genuine provenance** (must keep):
`Port of FUN_…` · `port of retail FUN_…` · `from retail FUN_…` ·
`Mirror of retail FUN_…` · `Equivalent to FUN_… inlined` ·
`FUN_… @ 0x<va> → name` (header map) · a section header
`FUN_… (NNN B) — …` immediately above the real implementation
(battlefield-render leaves) · inlined helpers whose token *also* appears
in the `.c` body (`engine_state_hash` `0x44015a` / `0x43ffc3`).

**Pollution** (should demote):
`FUN_…(args)` call-pattern · `FUN_… (NNN B)` in a TODO list ·
`callees … are still unported` · `retail calls/logs/fires FUN_…` ·
`FUN_… surveyed:/deferred/unknown/NOT invoked/stub`.

Tightening the generator to require `Port of` would drop the ~200 ports
that use other phrasings (only 39 lines say "Port of FUN_"). So the fix
is **convention + review**, not a counting-logic change: cite *ported*
functions by `FUN_<va>` token, cite *unported* neighbours by bare hex
`0x<va>`. See [`../AGENT-WORKFLOW.md`] commit conventions and the
roster-dispatch note in `PROGRESS.md` (2026-05-29).

## Detection — `tools/audit_provenance.py`

A warn-only auditor (does **not** modify the ledger). For every touched
function it checks whether any src reference is self-provenance
(`@ 0x<va>`, or `(port|mirror|equivalent) … FUN_<va>`, or
`from retail FUN_<va>`) and reports the rest in two tiers:

- **callish-only** — every reference is a `FUN_<va>(` call form. This is
  the one high-precision tell: a bare call-pattern in a comment is never
  a port. **Safe to demote.**
- **review** — no self-provenance, not callish-only. A *mix* of genuine
  ports (the section-header / inlined-helper / call-chain cases above)
  and pollution. Each needs a human to confirm the function is genuinely
  unimplemented (no body in any `.c`) before demoting.

Run it after `gen_port_ledger.py`:

```
nix develop -c python3 tools/audit_provenance.py
```

## What was corrected (2026-05-29)

Demoted **44** verified false promotions (token → bare hex), dropping the
headline from **316 touched (12.9 %, 13.7 % bytes)** to **272 touched
(11.1 %, 10.1 % bytes)**:

1. **3 callish, mixed refs** — `0x43eda0` (637 B scheduled-events
   dispatcher), `0x4afab9` (897 B status logger), `0x447136` (408 B
   delayed-action enqueue). All three were marked *tested*; demoting them
   also corrected the call-graph frontier (the enqueue cluster
   `0x4470c4/0x4476e4/0x447740/0x447026` had appeared as zero-dep leaves
   only because their sole "caller" — enqueue — was falsely promoted).
2. **16 callish-only** — engine.c roadmap byte-lists + `engine_handlers_da.h`
   summarizer notes + `gfx_sprite_render.h` logger sub-deps + audio.c
   voice-stop notes.
3. **25 `.h`-only, no self-provenance** — verified by reading each
   context: `engine_event_queue.h` unported callees (incl. the 10 915 B
   `0x47309e`), the deferred IO retry chain `0x4af116/0x4af312`, config
   blob loaders, `engine_handlers_da.h` handler roadmap, CRT
   malloc/free/memcpy wrappers, text_input file-IO.

Three heuristic false-positives were **kept** (genuine ports whose
provenance my first regex missed): `0x404ead` ("Port of FUN_00404ead.
Manhattan"), `0x448411` / `0x4479de` ("… port of FUN_… et al.").

## What was corrected (2026-05-31)

Demoted **6** verified false promotions introduced by the recent
game-start / RNG chips (token → bare hex), dropping the headline from
**387 touched (15.8 %)** to **381 (15.6 %)**, 351 → 345 tested, 1248
code-bytes off the touched total:

- **RNG chip** named two unported sibling seeders by token in
  `engine_rng.c` and `engine.h`: `0x46dbf5` (319 B), `0x46dd34` (209 B),
  plus their still-unported parent orchestrator `0x4676e0` (144 B).
- **territory chips** named three unported neighbours by token in
  `engine_territory.c`: `0x469370` (188 B) and `0x46e067` (317 B) — the
  two producers that *set* the render-flag bit `0x46df51` clears — and
  the reset-path caller `0x46d3da` (71 B).

Two tokens in `engine_territory.c` were initially over-demoted then
**reverted** — both are genuinely ported and so are correctly cited *by*
token: `0x45240a` (`engine_dirty_rect_mark_full_screen`, header-map
provenance in `engine_dirty_rect.h`) and `0x46ead0` (realized inline as
`clear_record` in `game_init.c`, inline-token annotation). Lesson: a
`grep "Port of"` miss does **not** prove unimplemented — confirm via the
header-map (`FUN_… @ 0x… → name`) and inlined-helper provenance forms
before demoting.

## Remaining work

`audit_provenance.py` still flags **~59 review-tier** functions — a dense
mix. Confirmed genuine (do **not** demote): the battlefield-render leaves
(`0x4bda92`, `0x4bdb95`, `0x4be070`, …), `engine_state_hash` inline
helpers (`0x44015a`, `0x43ffc3`), the gfx bring-up chain (`0x42eb7c`,
`0x42e5c0`, `0x42ec91`), audio template `0x425ded`. Confirmed pollution
still present (safe to demote on a future pass): the engine.c
`surveyed:` / `unported:` survey block (`0x49a3e6`, `0x4b12fb`,
`0x426529`, `0x4b1de0`, `0x4b0aaa`, `0x499a1c`, `0x498458`, `0x4b2636`,
`0x4978ad`, `0x40490d`), the main.c teardown notes (`0x4b26f8`,
`0x451f70`, `0x4343f8`, `0x425d35`, `0x42e3ba`), gfx.c surface-build
trio (`0x42f1df`/`0x42f449`/`0x42f306`). Left for a focused follow-up
because separating them reliably needs a per-function "is there a body in
a `.c`?" check, which is judgment, not a regex.
