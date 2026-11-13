# OpenLords2 — agent entry point

C reimplementation of **Lords of the Realm II** (Impressions / Sierra, 1996) as a drop-in for the
user's `Lords2.exe` (GOG 2.0.0.3, sha256 6fec043e…). Goal: **full behavioral parity with retail**,
proven against the user's own exe — not byte-identical, not an MVP. Legal line: **never redistribute
game assets or decompiled code** (gitignored: `vendor/`, `docs/decompiled/`, IDA output, saves, shots).

Dense on purpose; auto-loads every session. **The repo is the source of truth**; `~/.claude` memory
holds pointers only.

## Session start
1. `git log -n 5 --oneline`
2. `docs/FRONT.md` — the active front, ★ next micro-task, human queue.
3. At session end: report pending human items (FRONT §Human queue).

**Hosts.** **maximus** (this Arch box): agent host; **`tools/dev CMD`** = the pinned devshell (mingw, python
deps) — **every build/test command runs inside it**. It sources a cached `nix print-dev-env`; `nix develop -c`
works too but keeps a ~370 MB evaluator resident per command (22 of them were 8 GB of the 2026-10-23 OOM). **`ssh win`**: the Windows 11 box (OpenSSH
on cutestation:2222, PowerShell) — rhost's `win` target, IDA 9.4. `ssh wslop` (WSL) is not needed.

## Methodology (2026-10-23 reset — `docs/audits/2026-10-23-methodology-reset.md`)
**Product direction = hybrid** (owner, 2026-10-23): port functions replace retail's inside rhost, each
verified on identical state; the standalone build falls out once coverage is (near) complete.
**Verification foundation = rhost** (`tools/rhost/`, README): retail runs INSIDE our process with a
software DirectDraw / DirectSound / window layer — deterministic, headless, never fullscreen. Same trace
⇒ same state hash, on Windows and wine alike (0 masked slots). No Frida, no anchors, no pins.

**Porting loop (per function):**
1. **Read** IDA pseudocode + disasm: `/mnt/ephemeral/openlords2/decompiled-ida/` (`tools/ida/`).
   `docs/decompiled/all.c` (Ghidra) is secondary (drops x87 operands). Engine quirks:
   `docs/findings/engine-quirks.md`; per-subsystem RE: `docs/findings/INDEX.md`.
2. **Write** the C port (retail semantics). An existing pure in/out port gets a retail-layout wrapper.
3. **Verify vs retail on identical state:** adapter + row in a `tools/rhost/rh_hooks*.c` table (rhost.py
   reads the tables) → `rhost.py verify` (shadow per nesting pass, then the hybrid pass: every hook replaced,
   `corpus --check` — state + every presented frame). Iterate with `--target wine --only RE` (no evidence
   written). A port function never calls its own retail VA. Pattern + binding: `rh_hooks_ai.c`,
   `docs/findings/hybrid-pilot-ai.md`.
4. **Commit with an evidence line:** `rhost shadow: N calls / K scenarios, 0 divergences; replace:
   corpus --check PASS` (or the divergence + why).

**Gates.** `rhost.py det SCEN -n 3` (0 differing dwords; run it on Windows too — a host can be nondeterministic where
wine is not); `rhost.py corpus --check` (and `--target wine`; `--bless` runs each scenario twice and refuses a mismatch);
`rhost.py verify`. Port host tests: `tools/dev make -C tests run -j16`.

**Scenarios** `tests/scenarios/<s>/trace.jsonl` (`{"f","m","w","l"}` input events, 640×480 coords):
human play `rhost.py record NAME [--from S]` (visible window on the Windows desktop), authored with
`tools/rhost/rhbot.py "run 600; click 320 107; …"`, or played by the autoplay bot (`tools/bot/`).

**Agent-driven play (2026-11-13 — `tools/drive/README.md`, card `tools/drive/DRIVER.md`):** `./rd` drives retail LIVE
(`rhost --live`: retail as a coroutine, in-process savestates, exploration search): step, read state by name (`vars.json`
records `R(p)` / `C(k)` / `A(i)`, `count(REC, COND)`), `rd text` / `rd hot` / `rd look` (screen text, hotspots, the exact
frame), search on savestates (`-j N` over win / bunker / wine), `rd check` / `verify` / `cov`, `rd file` → scenario.
The Kirby playbook as openrecet runs it: a task = ordered state predicates (`tools/drive/tasks/`), the model names state
and designs the search, a driver subagent (the parent session's model) runs it; the API driver `tools/drive/agent.sh`
only when `agent.sh --check-credit` passes AND the owner says credit is available. Gates after touching rhost:
`rhost.py snap-check --target wine` (savestates ≡ straight replay). Hosts: `RD_HOST=auto` = Windows (`RHOST_WIN=win-wifi`
when on wifi; rd falls back to it) → bunker (`tools/bunker/`, root@10.0.10.58, pool `ol2-bunker-pool` 4 G) → wine.
**Live probing** (scripted) = `rhbot.py` (control channel) — probes: `--watch-write VA@F`, `--trap VA`, `--coverage`.

**Legacy — superseded, keep for reference, don't extend:** the Frida retail drive (`tools/frida/`,
`frida_capture.py`, `probe_daemon.py`, `probe.py`, `openlords2_mcp.py`, `play_*.py`, film tools),
anchor-segmented traces in `tools/traces/` (click coordinates still useful), Trace Studio v3's Frida
capture (the **viewer** stays for human eyes).

## Parity rules
- **Earlier "BYTE-EXACT" / "1:1" claims are UNVERIFIED** until re-checked under rhost.
- **Attribute a divergence before suspecting logic:** upstream inputs → RNG → phase → logic. rhost pins
  all of these for retail; a shadow compares on identical state, so a divergence there IS logic.
- **x87:** in-game CW 0x027F (53-bit). The port builds `-std=c11` (float literals evaluate as long double
  — openrecet found that diverges; prefer `gnu11` after a sweep).
- Engine quirks (retail ground truth only) → `docs/findings/engine-quirks.md`.
- **If a tool can't show you something, improve the tool.** Never hand-wave a diff.

## Build / run / test
- Port: `tools/dev make -C src -j16` → `build/openlords2.exe`. `src/gen/` (gitignored) is derived from the user's exe / the
  IDA export: `tools/gen_mapblit.py`, `tools/extract_*_table.py`. A git worktree symlinks `vendor`, `docs/decompiled`, `src/gen`.
- rhost: `python3 tools/rhost/rhost.py …` (auto build + stage). Results `/mnt/ephemeral/openlords2/rhost-runs/`.
- Windows: `C:\ol2-rhost\` (separate from the siblings' `C:\orc-rhost`, `C:\oss-rhost`, `C:\de-rhost`); kill only
  our own processes by full path / PID, never by name. Interactive jobs (record, --show) go through the
  scheduled task `ol2-rhost-runner`.
- Game data: maximus `vendor/original` → `/mnt/ephemeral/gog/Lords of the Realm II`; rhost uses pristine copies
  (`C:\ol2-rhost\game`, `/mnt/ephemeral/openlords2/rhost-wine/game`) without saves or harness leftovers.

## Conventions
- **Terse prose** in docs and commits; identifiers, hex, paths, numbers verbatim.
- **Batch** independent tool calls. **Delegate** mechanical / search work (wrapper batches, sweeps, exports)
  to subagents; keep RE interpretation and parity adjudication.
- **Commits**: logical units, without being asked; master only; push only when asked. The pre-commit hook
  (`tools/git-hooks`) regenerates STATUS / port-ledger.
- **Knowledge in `docs/`.** `docs/FRONT.md` holds only open work; resolved arcs → `docs/PROGRESS.md` or findings.
- **Ledger footgun:** any `FUN_<va>` token in `src/` counts as "ported" — name unported callees by bare VA.
- **Human verification:** never ask the human to reconstruct state; give one command and what to look for.

## Hard rules
- **maximus has 31 GB, no swap, shared with the owner's desktop apps.** Wrap commands in `tools/dev`, not
  `nix develop -c`. At most one whole-corpus `verify` / `corpus` per agent at a time; with two agents
  working, wine `-j 8` each. Leave nothing running when you report (stop your own batches by PID).
- **The Windows box is shared** with sibling projects running the same architecture right now: openrecet
  (`C:\orc-rhost`, task `orc-rhost-runner`, `rhost.exe`), OpenSummoners (`C:\oss-rhost`, `oss-rhost.exe`),
  OpenMare (`om-rhost-runner`), DioramaEngine (`C:\de-rhost`), their IDA dirs (`C:\orc-ida`, `C:\oss-ida`,
  `C:\de-ida`). Ours: `C:\ol2-rhost`, `C:\ol2-ida`, `ol2-rhost.exe`, task `ol2-rhost-runner`. Never read-write
  or delete outside ours, never kill by name (only a PID we started), never touch their tasks / queues.
  Keep Windows batches small (`-j 3` default) — overlapping jobs stall their gates; heavy batches and the
  pre-commit gate run on wine (identical results).
- Never `pkill -f` broad patterns (other projects run their own daemons / rhost / IDA jobs in parallel).
  The legacy Frida probe daemon stops only via `nix develop -c python3 tools/stop_probe.py`; legacy
  `tools/kill_retail.py` kills `Lords2.exe` by name — never pass it `--name`.
- Background waiters poll an output marker, never `pgrep -f` their own pattern.
- OpenRouter: only the project key `openlords2-bot` (read per process by `tools/bot/llm.py`; never print it).

## Where to read next
`docs/FRONT.md` · `docs/audits/2026-10-23-methodology-reset.md` · `tools/rhost/README.md` ·
`docs/findings/in-process-retail-host.md` · `docs/findings/INDEX.md` · `docs/STATUS.md` (derived) ·
`docs/PROGRESS.md`.

## Storage on maximus (2026-11-06)

- **On cold, not local:** `/mnt/ephemeral/openlords2/rhost-runs/` and `rhost-wine/root/runs/` (~62 GB of run
  output). The rest of `/mnt/ephemeral/openlords2/` (decompiled-ida, rhost-wine bin/game, prefixes) and the
  GOG install `/mnt/ephemeral/gog/` stay local. `/mnt/ephemeral/lords2-decomp/` is parked whole.
- **Park run output aggressively:** once a batch is no longer needed day to day (the work it was evidence for is committed), `offload park --fast <batch dirs>` — the copy in and out of cold is cheap (~100–250 MB/s), local disk is not.
- Before recreating any of it (re-running a batch, re-extracting, re-exporting, re-downloading), check cold: the stub `OFFLOADED.md` at the old path, `offload where <path>`, `offload list`; `offload fetch <path>` brings back the whole thing or any sub-path (the cold copy stays, so re-parking is cheap). `/mnt/ephemeral` is now a symlink to `~/data` on maximus rootfs (the btrfs array is retired); cold mirrors it 1:1 at `cold:/gigavault/staging/maximus-ephemeral/<same path>`. Policy: maximus `/opt/src/maximus/AGENTS.md` "Disk space".
