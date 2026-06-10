# OpenLords2 — agent operating notes

Most project knowledge lives in the auto-memory at
`~/.claude/projects/-opt-src-OpenLords2/memory/` (indexed by `MEMORY.md`,
loaded each session). This file holds only must-not-forget operational rules.

## Always run inside `nix develop`
Every build/test/tool command must run in the dev shell (commonly forgotten).
Prefix with `nix develop -c …` or open the shell first.

## frida / retail host — never treat as "down", never ask to start it
- The Windows-host frida-server is **normally up** at `cutestation.soy:27042`
  (shared with the sibling RE projects). It serves all capture/parity tooling.
- **If it is down, start it YOURSELF — UAC is auto-approved. Do NOT ask the
  user.** The tooling already does this: `frida_capture.ensure_frida_server`
  (used by `trace_studio capture` by default, `auto_start_server=True`) runs
  `Start-Process -Verb runAs …` to launch it. Just run the capture; the
  spawn-and-attach handles a cold host. You may also launch it directly via
  `powershell.exe -NoProfile -Command "Start-Process -Verb runAs -FilePath
  '<win path to frida-server.exe>' -ArgumentList '-l','0.0.0.0:27042'"`.
- The game itself does NOT need to be pre-running — `frida_capture` SPAWNS
  `retail.exe` and attaches; an idle host with no Lords process is normal.
- **Do not probe reachability with bash `/dev/tcp/cutestation.soy/27042`** —
  it gives false negatives here. Use the real client:
  `frida.get_device_manager().add_remote_device('cutestation.soy:27042')
  .enumerate_processes()`, or just run the capture and trust the tooling.

## Trace studio is the workhorse (see [[project_trace_studio]])
One trace drives BOTH engines → label-keyed pixel + save-schema STATE diff.
Hand-craft traces and add anchors as needed to keep port↔retail in sync — the
game is simple enough that bespoke traces beat generic capture for most chips.
`capture --only port` is the fast loop against cached retail; `--target both`
re-runs retail live.

**Keep the studio SERVER running so the user can look** (it's their primary
way to verify work + flag divergences). If nothing is listening on :8780,
start it yourself in the background — don't ask:
```
ss -ltnp | grep -q ':8780' || \
  nix develop -c python3 tools/trace_studio.py serve --session <name> &
```
URL: `http://localhost:8780/?session=<name>`. Re-point at the latest session
after a capture. The baseline workflow is: craft a REPRODUCIBLE trace, pin the
non-deterministic parts with anchors (and `--turbo` on the port to match
retail's virtual clock / sub-step rate) so BOTH sides run frame-by-frame exact
up until a REAL divergence — then the divergence stands out cleanly. The
sibling projects ../openrecet and ../OpenSummoners do this well; mirror them.
