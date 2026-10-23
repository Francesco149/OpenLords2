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

**Hosts.** **maximus** (this Arch box): agent host; `nix develop -c …` = the pinned devshell (mingw,
python deps) — **every build/test command runs inside it**. **`ssh win`**: the Windows 11 box (OpenSSH
on cutestation:2222, PowerShell) — rhost's `win` target, IDA 9.4. `ssh wslop` (WSL) is not needed.

## Methodology (2026-10-23 reset — `docs/audits/2026-10-23-methodology-reset.md`)
**Verification foundation = rhost** (`tools/rhost/`, README): retail runs INSIDE our process with a
software DirectDraw / DirectSound / window layer — deterministic, headless, never fullscreen. Same trace
⇒ same state hash, on Windows and wine alike (0 masked slots). No Frida, no anchors, no pins.

**Porting loop (per function):**
1. **Read** IDA pseudocode + disasm: `/mnt/ephemeral/openlords2/decompiled-ida/` (`tools/ida/`).
   `docs/decompiled/all.c` (Ghidra) is secondary (drops x87 operands). Engine quirks:
   `docs/findings/engine-quirks.md`; per-subsystem RE: `docs/findings/INDEX.md`.
2. **Write** the C port (retail semantics). An existing pure in/out port gets a retail-layout wrapper.
3. **Verify vs retail on identical state:** wrapper + row in `tools/rhost/rh_hooks.c` (+ `VERIFY_HOOKS`
   in rhost.py) → `rhost.py verify` (shadow) and `rhost.py corpus --check --hook replace:NAME` (hybrid,
   state + every presented frame). A port function never calls its own retail VA.
4. **Commit with an evidence line:** `rhost shadow: N calls / K scenarios, 0 divergences; replace:
   corpus --check PASS` (or the divergence + why).

**Gates.** `rhost.py det SCEN -n 3` (0 differing dwords); `rhost.py corpus --check` (and `--target wine`);
`rhost.py verify`. Port host tests: `nix develop -c make -C tests run -j16`.

**Scenarios** `tests/scenarios/<s>/trace.jsonl` (`{"f","m","w","l"}` input events, 640×480 coords):
human play `rhost.py record NAME [--from S]` (visible window on the Windows desktop), authored with
`tools/rhost/rhbot.py "run 600; click 320 107; …"`, or played by the autoplay bot (`tools/bot/`).

**Live probing** = `rhbot.py` (control channel: step frames, post input, read memory, shots) — it
replaces the old Frida probe daemon / MCP. Probes: `--watch-write VA@F`, `--trap VA`, `--coverage`.

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
- Port: `nix develop -c make -C src -j16` → `build/openlords2.exe`.
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
- Never `pkill -f` broad patterns (other projects run their own daemons / rhost / IDA jobs in parallel).
  The legacy Frida probe daemon stops only via `nix develop -c python3 tools/stop_probe.py`.
- Background waiters poll an output marker, never `pgrep -f` their own pattern.
- OpenRouter: only the project key `openlords2-bot` (read per process by `tools/bot/llm.py`; never print it).

## Where to read next
`docs/FRONT.md` · `docs/audits/2026-10-23-methodology-reset.md` · `tools/rhost/README.md` ·
`docs/findings/in-process-retail-host.md` · `docs/findings/INDEX.md` · `docs/STATUS.md` (derived) ·
`docs/PROGRESS.md`.
