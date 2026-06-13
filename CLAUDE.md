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

## Trace Studio v3 is the workhorse (see [[project_trace_studio_v3]])
`tools/trace_studio_v3/` — a NATIVE viewer (no web, no PNG bake). One trace
drives BOTH engines through a contiguous capture WINDOW; each frame is stored as
its 8bpp index plane + palette (dedup'd) in a compact `.olv3` container, and the
viewer reconstructs any frame on demand (sub-ms, pure CPU). It diffs at the
INDEX level (blitter output), the PALETTE level (palette builder), and RGB, plus
the save-schema STATE panel — and scrubs the window port|retail|diff. The old
web studio (`tools/trace_studio` + `_web`, the `:8780` server) is RETIRED.

Daily loop:
```
# drive both engines through a trace → containers + view.json → launch viewer
nix develop -c python3 tools/trace_studio_v3/olv3_capture.py --trace grid_load_dense
# fast port-only re-capture against cached retail (don't relaunch):
nix develop -c python3 tools/trace_studio_v3/olv3_capture.py --trace <t> --only port --no-view
```
The viewer is `tools/trace_studio_v3/viewer/viewer.exe` (a Windows process via
WSLInterop). **The user verifies in-game work by scrubbing this viewer** — after
a capture, launch it for them (or `--shot out.bmp --col N view.json` headless,
then push the BMP to the llm-feed). Build it with
`nix develop -c make -C tools/trace_studio_v3/viewer`.

Bespoke traces beat generic capture; `{caprange:[START,COUNT]}` gives the
contiguous window, anchored to a semantic event so the port↔retail load-stretch
cancels (both capture at `anchor+offset`). `--turbo` on both sides matches
retail's virtual clock so they run frame-exact up to a REAL divergence. The
sibling ../openrecet `tools/trace_studio_v3/` is the reference; mirror it.
