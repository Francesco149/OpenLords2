# Agent workflow — autonomy conventions

How Claude (the model running this RE project) should structure its work.
Read at the start of every new session.

## TL;DR

- **Stay autonomous.**  The user expects to be a periodic visual-
  confirmation checkpoint, not a per-step approver.  Make choices and
  report them.
- **Subagents are forbidden by default** (see `PLAN.md` §7).  Inline
  work, even if it's long.  The only carve-outs are pure mechanical
  batch tasks with no judgment surface.
- **Everything runs inside `nix develop`.**  `python3`, `frida`,
  `ghidra-*`, `i686-w64-mingw32-gcc` are all flake-provided; outside
  the dev shell they will not be on PATH.

## Stop-and-report points

The orchestrator should pause when:

- A planning decision is needed (license, scope, format choice).
- A risky action is about to happen (git commit on a shared branch,
  force-push, anything destructive to `vendor/`).
- A subsystem milestone is complete and the next milestone is a
  fresh scope worth talking about.
- The harness asks for visual confirmation (a side-by-side image is
  generated; relay the path to the user).

These are *natural* stop points — the user doesn't have to
interrupt; the orchestrator pauses on its own and reports.

### Suggest `/clear` at natural milestones

The PROGRESS log + findings docs are the durable memory of this
project; the conversation context is a working set that gets bloated
by every file read of large Ghidra dumps (`docs/decompiled/all.c`
alone will be multiple MB).  **At every natural stop-and-report
point, suggest the user run `/clear`** if the just-finished
milestone was a clean unit and the next milestone is a fresh scope.

Skip the `/clear` suggestion when:

- The current task is mid-flight (a hook is half-implemented, a test
  is failing, a finding is partial).
- The next planned step depends specifically on a fact just learned
  this session that hasn't yet been committed to a doc.

## Commits

**Commit in logical units as you go**, don't let the working tree
pile up.  A good unit is "one subsystem ported", "one extractor +
its format spec", "one harness fix + the test that proves it", "one
finding documented with its supporting Ghidra reads".  When in
doubt, commit at every natural stop-and-report point.

Conventions:

- Use the user's global git identity (`headpats`).  Never
  `git config --local user.email` something else.
- **Every commit Claude makes must include a co-author trailer**:

      Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>

  The `commit-msg` git hook (`tools/git-hooks/commit-msg`, wired via
  `core.hooksPath` by `tools/setup.sh`) enforces this: it rewrites
  any stale `Claude Opus 4.<N>` line to the current model and appends
  the trailer if missing, so the model version lives in exactly one
  place.  Use a HEREDOC to pass the message so the trailer lands on
  its own line.
- Commit only what was asked.  Never `git add -A` and sweep in
  stray files.
- Never `git push` unless the user explicitly asks.
- Never amend (`--amend`) or rebase (`-i`) without explicit
  request — a new commit is almost always safer.
- Never bypass hooks (`--no-verify`) without explicit request.

## Note engine quirks as you find them

When porting a function or chasing a bug, you will routinely
stumble on small things the original engine does that are weird,
charming, or inexplicable — non-standard hash variants, hand-rolled
obfuscation, hard-coded retry loops, deliberate-looking buffer
over-allocations, load-bearing-by-accident behavior, etc.

**Append a short entry to
[`docs/findings/engine-quirks.md`](findings/engine-quirks.md)**
before moving on.  Even one paragraph.  Even if you're not sure
it's interesting — it almost certainly is once you write it down.

Why this lives here, not in code comments: each quirk is a one-
paragraph story about *why* the engine does the weird thing, and
that story rots quickly if scattered across `// note:` comments
next to faithful ports.  Collecting them in one file gives the user
a fun read, signposts future debugging, and lets you cite a single
anchor in commit messages.

Format: numbered entry, 1–3 short paragraphs, optional code
snippet, and a `> 📍` pointer to the source location.

## Findings docs

Per-subsystem findings live in `docs/findings/<subsystem>.md`.
Each doc captures the **why** behind our implementation choices:
which Ghidra addresses cover which behavior, what the calling
convention is, which globals are read/written, what subtle
behavior the obvious port would miss.

Findings docs are the *durable* output of a session — they
outlive the conversation context.  If you spend an hour figuring
out how a subsystem works, the next session shouldn't have to
re-derive it.

The map of all findings docs is
[`docs/findings/INDEX.md`](findings/INDEX.md) — add a row there when
you create a new one.

## Harness usage

Before any retail-binary probing:

1. Confirm `frida-server.exe` is reachable on the Windows host
   (`tools/smoke-test.py` will UAC-prompt to auto-start it if
   needed).
2. Always launch via the harness with `--hide-window`, `--turbo`,
   and `--silent-audio` enabled.  The retail Lords2.exe is known
   to:
   - Change desktop resolution (DDraw exclusive-mode fullscreen).
   - Play loud audio on boot (Sierra startup chime).
   - Show modal error dialogs if DirectX or save-game files are
     missing.
   All three are disruptive in a dev-loop context.
3. Capture artifacts under `runs/<scenario>/<timestamp>/`.

## Verify ported functions against retail (don't skip this)

Host unit tests only prove the port is internally consistent with the mental
model you wrote them from — a wrong model passes its own tests.  The byte-exact
check is the **pure-function diff vs retail**:

```fish
nix develop -c python3 tools/diff_test.py --functions <name> \
    --vectors 64 --remote cutestation.soy:27042
```

It snapshots→injects→calls retail's `FUN_*`→reads-back→restores and diffs
against `tests/build/libengine_diff.so` (the port).  Runs in turbo (fast).  This
caught an inverted predicate in the dev-spread port that the unit tests shared
and passed.  Add a target for each meaningfully stateful gameplay function you
port — for the `__stdcall void()` per-realm passes it's cheap (reuse the generic
`runRetailRealmPass` RPC).  See `docs/findings/pure-function-diff.md`.

The toolset (call-graph diff `tools/call_trace_diff.py`, mem-watch
`tools/mem_watch.py`, frame diff `tools/compare_frames.py`, the
`tools/frida_capture.py` driver, the `tools/feed` llm-feed push wrapper) is
current and documented in `docs/harness-roadmap.md` — reach for it rather than
deferring anything that "needs the game running."

## When to use Ghidra vs r2

- **Ghidra** for primary decompilation.  Headless export via
  `tools/ghidra-headless.sh` produces `docs/decompiled/all.c` and
  per-function files; that's the bulk-search anchor.
- **r2** for functions Ghidra's decompiler chokes on (heavily
  optimized math, unusual calling conventions, obfuscated jump
  tables).  See the sibling projects' findings for examples of
  when r2's view is clearer.
