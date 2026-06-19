#!/usr/bin/env python3
"""
tools/frida_capture.py — OpenLords2 Frida-driven retail harness.

Connects to a Windows-side frida-server (default 127.0.0.1:27042,
shared with the sibling RE projects), spawns
`vendor/original/Lords2.exe` with `tools/frida/openlords2-agent.js`
loaded, and lays artifacts down in a scenario-compatible run dir:

    <run_dir>/
        agent.log                     (Frida send(log) + errors)
        run.json                      (metadata)

Phase A capability set:
    --hide-window         multi-API hide on all owned HWNDs
    --turbo               kernel32!Sleep no-op (turbo v1)
    --silent-audio        winmm waveOutSetVolume + DSound buffer SetVolume → 0
    --force-windowed      ddraw / dsound cooperative-level rewrite + GDI stub

The CLI exposes everything; module-level `run_capture()` is what
scenario tests will call once we have scenarios.

frida-server setup (Windows side, one-time — same as OpenMare):
    1. Download frida-server-<ver>-windows-x86_64.exe matching the
       Python frida version in nix.
    2. Rename → frida-server.exe, run as Administrator (or use the
       auto-start helper below — needs UAC approval).
    3. Listens on 0.0.0.0:27042 so WSL can reach it across NAT.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import frida


ROOT       = Path(__file__).resolve().parent.parent
AGENT_JS   = ROOT / "tools" / "frida" / "openlords2-agent.js"
RETAIL_EXE = ROOT / "vendor" / "original" / "Lords2.exe"
ASSET_CWD  = ROOT / "vendor" / "original"

# Default endpoint is cutestation.soy, NOT 127.0.0.1 — WSL2's Hyper-V firewall
# blocks the loopback path to the Windows-host frida-server, and a 127.0.0.1
# default also triggers a spurious elevated frida-server *launch* attempt
# (UAC prompt) when the loopback connect times out.  Override via env if your
# frida-server is elsewhere.  (See memory reference_frida_remote.)
DEFAULT_REMOTE = os.environ.get(
    "OPENLORDS2_FRIDA_REMOTE",
    os.environ.get("OPENMARE_FRIDA_REMOTE",
                   os.environ.get("OPENRECET_FRIDA_REMOTE", "cutestation.soy:27042")))

# Where to look for the Windows-side frida-server.exe when auto-starting.
# Same path layout the siblings use (one install serves all three projects).
DEFAULT_FRIDA_SERVER_EXE = Path(os.environ.get(
    "OPENLORDS2_FRIDA_SERVER_EXE",
    os.environ.get(
        "OPENMARE_FRIDA_SERVER_EXE",
        os.environ.get(
            "OPENRECET_FRIDA_SERVER_EXE",
            f"/mnt/c/Users/headpats/Documents/_devtools/"
            f"frida-server-{frida.__version__}-windows-x86_64/"
            f"frida-server-{frida.__version__}-windows-x86_64.exe"))))


# ─── helpers ──────────────────────────────────────────────────────────────


def wslpath_w(p: Path) -> str:
    """Translate a Linux path to its Windows form for frida-server.exe."""
    r = subprocess.run(
        ["wslpath", "-w", str(p)],
        capture_output=True, text=True, check=True,
    )
    return r.stdout.strip()


def _tcp_open(host: str, port: int, timeout: float = 1.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def ensure_frida_server(remote: str, exe_wsl_path: Path,
                        startup_timeout_s: float = 15.0) -> bool:
    """Ensure frida-server.exe is reachable at `remote`. If not, spawn it
    via `Start-Process -Verb runAs` on the Windows side (UAC will prompt)
    and poll until the port answers. Returns True if reachable.

    Idempotent: if already up, returns True immediately.  Same elevation
    flow OpenMare/openrecet use — and the same frida-server.exe instance
    can serve all three projects.
    """
    host, _, port_s = remote.partition(":")
    port = int(port_s or "27042")
    if _tcp_open(host, port, timeout=1.0):
        return True

    if not exe_wsl_path.exists():
        print(f"[ensure_frida_server] {exe_wsl_path} not found; cannot "
              f"auto-start. Override with $OPENLORDS2_FRIDA_SERVER_EXE or "
              f"start manually.",
              file=sys.stderr)
        return False

    win_exe = subprocess.run(
        ["wslpath", "-w", str(exe_wsl_path)],
        capture_output=True, text=True, check=True).stdout.strip()
    print(f"[ensure_frida_server] launching elevated: {win_exe} "
          f"(approve the UAC prompt)", file=sys.stderr)

    ps_cmd = (
        f"Start-Process -Verb runAs -WindowStyle Normal "
        f"-FilePath '{win_exe}' "
        f"-ArgumentList '-l','0.0.0.0:{port}'")
    subprocess.run(
        ["powershell.exe", "-NoProfile", "-Command", ps_cmd],
        check=False)

    deadline = time.monotonic() + startup_timeout_s
    while time.monotonic() < deadline:
        if _tcp_open(host, port, timeout=0.5):
            print(f"[ensure_frida_server] up on {remote}", file=sys.stderr)
            return True
        time.sleep(0.5)
    print(f"[ensure_frida_server] timed out waiting for {remote}",
          file=sys.stderr)
    return False


# ─── capture session ──────────────────────────────────────────────────────


@dataclass
class CaptureConfig:
    duration_ms:       int  = 10_000
    remote:            str  = DEFAULT_REMOTE
    exe:               Path = RETAIL_EXE
    cwd:               Path = ASSET_CWD
    auto_start_server: bool = True
    server_exe:        Path = DEFAULT_FRIDA_SERVER_EXE
    hide_window:       bool = True
    silent_audio:      bool = True
    force_windowed:    bool = True
    # Default ON.  With force_windowed we still rewrite the cooperative level to
    # windowed, but we *skip* the DAT_0053f228=0 override so retail builds its
    # full-color palette (FUN_0042f306 — dark-red caps + bright-red hover
    # highlight) instead of the 8bpp blackout (FUN_0042f449).  That blackout is
    # only what a real player sees on a true 8bpp desktop (essentially never);
    # on any full-color desktop retail renders the reds, so the full-color
    # palette is the faithful default.  Keeping it on means windowed captures
    # match real fullscreen retail bit-for-bit, with no fullscreen screen-hog
    # (the window degrades to a desktop-res popup — cosmetic; --show-window
    # resizes it to 640×480, and we read the back surface anyway).  Disable with
    # --force-8bpp-palette only to regress against the old 8bpp groundtruths.
    # No-op when force_windowed is off (true fullscreen already builds it).
    # See engine-quirks #115.
    keep_retail_palette: bool = True
    turbo:             bool = False
    turbo_step_ms:     int  = 17        # virtual-ms per per-tick driver iteration
    # GDI present seat — intercept retail's IDirectDrawSurface::Blt to
    # primary and re-route through StretchDIBits.  DWM never propagates
    # DDraw NORMAL-mode Blts to the window's backing store, so without
    # this seat windowed retail under the harness renders to an
    # invisible primary surface.  See engine-quirks.md / the
    # project_dwm_ddraw_normal memory.
    gdi_seat:          bool = True
    # Agent-driven continuous GDI present.  Interactive-only — drives a
    # GDI present every N engine_ticks regardless of whether retail
    # itself Blts to primary.  Lets a human watching the window see the
    # engine render during stretches where retail's own present pipe is
    # dormant (Smacker intro, dirty-rect lulls).  Pairs with
    # show_window_after_ms to un-hide the window post-init.
    agent_present:               bool = False
    agent_present_every_ticks:   int  = 100
    # Interactive real-mouse seat.  Pokes the main-loop present gate
    # (var_app_active=1 / var_surface_lost=0) every tick so the engine
    # runs full-speed and services input even though a frida-spawned
    # window never wins foreground/WM_ACTIVATEAPP (without this the loop
    # Sleep(200)s and the window feels frozen).  Pairs with agent_present
    # (shows the frames) + show_window_after_ms.  See --interactive.
    force_active:                bool = False
    # When > 0, the agent calls ShowWindow(hwnd, SW_SHOW) on the engine's
    # main HWND once this many real-ms have elapsed since init.  Lets
    # hide_window survive engine bring-up (which dies under --no-hide
    # for reasons orthogonal to this work) then make the window visible
    # so the agent-driven present has somewhere to land.
    show_window_after_ms:        int  = 0
    # Client-area size to resize the engine's window to after show.
    # 640×480 is the engine's native render resolution (GFX_RENDER_W/H,
    # also DAT_004eb274 / DAT_004eac4c) — 1:1 pixel-perfect, no
    # scaling.  User-verified by zooming in.  Our GDI StretchDIBits
    # seat scales transparently so any other size works too via the
    # --show-window-client-* CLI flags.
    show_window_client_w:        int  = 640
    show_window_client_h:        int  = 480
    frame_capture:              bool = False
    frame_capture_max:          int  = 16   # 0 = unlimited
    frame_capture_warmup_secs:  int  = 5    # skip captures for first N cum-secs
    # One-shot globals dump: list of {va, size, label}.  Read once at the
    # first captured frame; written to <run_dir>/globals_dump.json.  Used
    # to seed a parity port's state from retail's exact values.
    dump_globals:      list[dict[str, Any]] | None = None
    # One-shot large-region dump: list of {va, size, label, deref}.  Read once
    # at the first captured frame; each region written as raw bytes to
    # <run_dir>/dump/<label>.bin.  `deref` reads the 32-bit pointer at `va`
    # first (heap-allocated sprite-bank base), then `size` bytes from there.
    dump_regions:      list[dict[str, Any]] | None = None
    # Globals poke: list of {va, bytes}.  Written into engine memory at the
    # top of every tick (survives the engine's own writes) to force
    # scenario state — e.g. a name-box string — before a capture.
    poke_globals:      list[dict[str, Any]] | None = None
    # Skip the boot intro movie chain (intro.smk → imptitle.smk → credits.smk).
    # The agent snaps each Smacker movie to its last frame so the engine runs
    # its own intro→title (state 0x1f) transitions in ~3 frames instead of the
    # ~23 real-sec turbo fast-forward.  Lets menu/UI capture runs use a tiny
    # --frame-capture-warmup-secs.  See tools/frida/openlords2-agent.js.
    skip_intro:                 bool = False
    # Input recording: when set, the WndProc hook emits one JSONL line per
    # dispatched input message to <run_dir>/input_trace.jsonl.
    input_record:      bool = False
    # Input replay: path to a JSONL trace produced by input_record. Loaded
    # eagerly and shipped to the agent inline via init opts (the Windows-side
    # agent has no access to WSL filesystem paths).
    input_replay_path: Path | None = None
    # Anchor-segmented input trace (TAS framework P2): superset of input_replay
    # with {wait}/{wait_until}/{capture} ops + anchor-relative frame counting.
    # Loaded eagerly, normalised to ops, shipped inline via init opts.  Implies
    # anchor detection + a frames dir (its {capture} ops grab the back surface).
    input_segtrace_path: Path | None = None
    # Call tracer (Phase G.3): hook each Ghidra-VA in call_trace_vas on
    # retail and emit per-tick call_trace_batch events → run_dir/call_trace.jsonl
    # (one row per call, schema {va, ret_va, frame}).  call_trace_frames
    # (optional) restricts emission to the listed sim frames.  Diff the
    # result against the port's --call-trace JSONL with call_trace_diff.py.
    call_trace:        bool = False
    call_trace_vas:    list[int] = field(default_factory=list)
    call_trace_frames: list[int] = field(default_factory=list)
    # Cum-sec-gated window: start emitting call-trace once the engine reaches
    # this cumulative-second (i.e. the target scene, reached under turbo), then
    # emit for call_trace_max_frames ticks. 0 = use the tick frame-filter.
    call_trace_min_cum_secs: int = 0
    call_trace_max_frames:   int = 8
    # Memory-access watch (Phase H): arm MemoryAccessMonitor over these regions
    # ([{va,size,label,access}], Ghidra VAs) and emit mem_access_batch events →
    # run_dir/mem_watch.jsonl. tools/mem_watch.py ranks the trapped writers.
    mem_watch:         bool = False
    mem_watch_regions: list[dict] = field(default_factory=list)
    mem_watch_precise: bool = True
    # Lever 4 — configurable re-arm budget (0 → agent default 8000).
    mem_watch_rearm_cap: int = 0
    # Lever 1 — bracketed arm window {"enter":VA,"leave":VA} (Ghidra VAs): arm
    # MemoryAccessMonitor only between ENTER's onEnter and LEAVE's onLeave, so a
    # read-hot page's neighbour traffic only burns the budget during the call.
    mem_watch_arm_between: dict | None = None
    # Lever 2 — snapshot-diff regions ([{va,size,label}], Ghidra VAs): poll each
    # tick and emit changed bytes → run_dir/mem_snapshot.jsonl.  Op-agnostic /
    # read-heat-immune; localises the writer's TICK (not the faulting insn).
    mem_snapshot_regions: list[dict] = field(default_factory=list)
    # Deterministic anchors (TAS framework): emit {anchor,frame} on engine-state
    # / screen-id transitions → run_dir/anchors.jsonl, keyed on the per-tick
    # counter.  See docs/plans/tas-framework.md.
    anchor_trace:      bool = False
    # Trace Studio v3 draw-stepping: a CModule mirrors the port's draw journal
    # (base + per-dispatch write-deltas vs the locked back buffer) at segtrace
    # {capture} ticks → run_dir/frames/frame_<idx>.draws (same OLDR sidecar the
    # port emits). Makes a retail frame draw-steppable / cross-engine diffable.
    capture_draws:     bool = False
    # Force a full strategic re-render on each {capture} tick so the map composites
    # into the draw journal (cross-engine map-draw diff, aligned zone). Sets the
    # retail demand-redraw global DAT_0057d340 = 2 before the tick.
    capture_force_redraw: bool = False
    # Neuter retail's strat-map mouse edge-scroll (FUN_00432221) so the captured
    # camera is the loaded-save scroll, not whatever the headless host cursor's
    # screen-edge position scrolled it to (nondeterministic vs the port, which has
    # no edge-scroll). Default off; trace_studio parity captures turn it on.
    suppress_edge_scroll: bool = False
    # Save sandbox: Windows path to a per-run dir into which the agent redirects
    # every save-file open (the .sav backup family + Castles.dat), so a capture
    # replay never reads or writes the real game-dir saves (reproducibility —
    # installSaveRedirectHook). None = off (real saves used in place). The caller
    # pre-seeds the sandbox with the reference save (olv3_capture does this).
    save_sandbox:      str | None = None
    # Save-schema state dump at segtrace {capture} instants:
    # [{i,va,size}] → run_dir/state/state_<tick>.json (trace_studio parity).
    state_dump_slots:  list[dict] = field(default_factory=list)
    # Per-frame state watch: [{name,va,type}] → run_dir/watch.jsonl (edge-
    # emitted).  The state oracle for authoring wait_until predicates.
    watch:             list[dict] = field(default_factory=list)
    # RNG seed pin: None = off; {} = retail-canonical defaults; or {a,b,single}.
    # Forces the new-game map RNG so port↔retail comparisons generate the same
    # map (the two targets don't seed from the same wall-clock instant).
    rng_seed:          dict | None = None
    exe_args:          list[str] = field(default_factory=list)


@dataclass
class CaptureResult:
    exit_code:  int = 0
    elapsed_ms: int = 0
    hwnd_count: int = 0
    hide_count: int = 0
    msgbox_count: int = 0
    cooplevel_count: int = 0
    audio_mute_count: int = 0
    fps_sample_count: int = 0
    last_current_fps: int = 0
    last_avg_fps:     int = 0
    last_cum_seconds: int = 0
    frame_count:      int = 0
    input_event_count: int = 0
    input_replay_post_count: int = 0
    gdi_seat_present_count: int = 0
    call_trace_calls: int = 0
    anchor_count:     int = 0
    anchors:          list[dict[str, int | str]] = field(default_factory=list)


# ─── BMP writer ───────────────────────────────────────────────────────────
#
# 24-bpp top-down BMP, mirrors src/gfx.c:gfx_capture_backbuffer.  We don't
# write 8-bpp paletted BMPs — every viewer handles 24-bpp uniformly, and the
# size delta (922 KB vs 308 KB for 640×480) doesn't matter for the
# verification stream.


def _write_paletted_bmp(out: Path, width: int, height: int,
                        palette: bytes, pixels: bytes) -> None:
    """Write a 24-bpp top-down BMP from 8-bpp paletted source.

    `palette` is 256 PALETTEENTRYs (R, G, B, peFlags) — 1024 bytes.
    `pixels`  is width*height bytes of palette indices, tight-packed.
    """
    row_bgr   = width * 3
    row_pad   = (4 - (row_bgr & 3)) & 3
    row_total = row_bgr + row_pad
    img_size  = row_total * height
    file_size = 14 + 40 + img_size

    with out.open("wb") as fp:
        # BITMAPFILEHEADER (14 bytes)
        fp.write(b"BM")
        fp.write(struct.pack("<I", file_size))
        fp.write(b"\x00\x00\x00\x00")          # reserved
        fp.write(struct.pack("<I", 54))        # bfOffBits = 14 + 40
        # BITMAPINFOHEADER (40 bytes), biHeight negative ⇒ top-down rows.
        fp.write(struct.pack(
            "<IiiHHIIiiII",
            40, width, -height, 1, 24, 0, img_size, 0, 0, 0, 0))
        # Pixel data — palette lookup, BGR byte order, row padding.
        pad = b"\x00" * row_pad
        for y in range(height):
            row_in = pixels[y * width : (y + 1) * width]
            row_out = bytearray(row_total)
            j = 0
            for idx in row_in:
                # PALETTEENTRY: R, G, B, peFlags.  BMP wants B, G, R.
                p = idx * 4
                row_out[j    ] = palette[p + 2]   # B
                row_out[j + 1] = palette[p + 1]   # G
                row_out[j + 2] = palette[p    ]   # R
                j += 3
            if row_pad:
                row_out[row_bgr:] = pad
            fp.write(row_out)


def _load_input_trace(path: Path) -> list[dict[str, int]]:
    """Read a JSONL input trace. Each line must decode to an object with
    integer fields t/m/w/l (tick, msg, wparam, lparam). Blank lines and
    lines starting with `#` are skipped — same convention as the input
    traces in the sibling projects.
    """
    out: list[dict[str, int]] = []
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        try:
            obj = json.loads(s)
        except json.JSONDecodeError as e:
            raise ValueError(f"{path}:{lineno}: {e}") from e
        try:
            out.append({"t": int(obj["t"]),
                        "m": int(obj["m"]),
                        "w": int(obj["w"]),
                        "l": int(obj["l"])})
        except (KeyError, TypeError, ValueError) as e:
            raise ValueError(
                f"{path}:{lineno}: missing/invalid t/m/w/l field: {e}") from e
    return out


def _load_segtrace(path: Path) -> list[dict[str, Any]]:
    """Read an anchor-segmented input trace (TAS framework P2) and normalise
    each line into an op dict with a `kind` the agent consumes directly.

    Line forms (JSONL; blank / `#` lines skipped):
      {"t":N,"m":M,"w":W,"l":L}                       → input  (t is RELATIVE
                                                         to the current segment's
                                                         opening anchor frame)
      {"wait":"ANCHOR"}                               → wait
      {"wait_until":{"va":"0x..","type":"u8","op":"<=","val":V}} → wait_until
      {"capture":N}  or  {"capture":N,"label":"..."}  → capture (anchor-relative)
    """
    ops: list[dict[str, Any]] = []
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        try:
            obj = json.loads(s)
        except json.JSONDecodeError as e:
            raise ValueError(f"{path}:{lineno}: {e}") from e
        try:
            if "wait" in obj:
                ops.append({"kind": "wait", "anchor": str(obj["wait"])})
            elif "wait_until" in obj:
                wu = obj["wait_until"]
                va = wu["va"]
                ops.append({"kind": "wait_until",
                            "va": int(va, 0) if isinstance(va, str) else int(va),
                            "type": str(wu.get("type", "i32")),
                            "op": str(wu["op"]),
                            "val": wu["val"]})
            elif "capture" in obj:
                ops.append({"kind": "capture", "n": int(obj["capture"]),
                            "label": str(obj.get("label", ""))})
            elif "auto_dismiss" in obj:
                # Reactive popup auto-dismiss toggle (mirrors the port's
                # {"auto_dismiss":N} op): applied immediately when the cursor
                # reaches it (no frame gate).  The agent's per-tick hook then
                # dismisses any open modal scroll while it's on.
                ops.append({"kind": "auto_dismiss",
                            "n": int(obj["auto_dismiss"])})
            elif "m" in obj:
                # w/l default to 0 when omitted — same leniency as the port's
                # C parser (src/input_trace.c), so one trace parses on both.
                ops.append({"kind": "input", "t": int(obj["t"]),
                            "m": int(obj["m"]), "w": int(obj.get("w", 0)),
                            "l": int(obj.get("l", 0))})
            else:
                raise ValueError("unrecognised op (need m / wait / wait_until / "
                                 "capture / auto_dismiss)")
        except (KeyError, TypeError, ValueError) as e:
            raise ValueError(f"{path}:{lineno}: {e}") from e
    return ops


# ─── leak-proofing helpers ─────────────────────────────────────────────────
#
# Retail is spawned by the UAC-elevated frida-server, so the spawned process
# runs ELEVATED and only the frida device can kill it (non-admin WSL kill /
# taskkill.exe are powerless).  The teardown therefore MUST route every kill
# through `device.kill`, and must do so even when a fragile synchronous frida
# RPC (get_gdi_seat_stats / unload / detach) hangs.  These helpers + the
# watchdog/heartbeat in _run_capture_impl make a leak require BOTH the
# controller being SIGKILLed AND the in-target heartbeat watchdog being
# disabled — and even then the next run's pre-reap collects the orphan.

# How long past the run deadline the controller-side watchdog waits for a
# clean teardown before force-killing the target and hard-exiting.
WATCHDOG_GRACE_S = 12.0
# Bound for each individual fragile frida call during teardown.
TEARDOWN_CALL_TIMEOUT_S = 4.0
# Controller → agent heartbeat cadence; the agent self-terminates if it goes
# silent for HEARTBEAT_TIMEOUT_MS (covers the controller being SIGKILLed).
HEARTBEAT_INTERVAL_S  = 2.0
HEARTBEAT_TIMEOUT_MS  = 12_000


def _call_with_timeout(fn, timeout: float, default=None):
    """Run `fn()` in a daemon thread; return its result, or `default` if it
    doesn't finish within `timeout`.  A hung frida RPC can't block teardown."""
    box: dict = {"v": default, "done": False}

    def _run():
        try:
            box["v"] = fn()
        except Exception:  # noqa: BLE001 — teardown is best-effort
            pass
        finally:
            box["done"] = True

    th = threading.Thread(target=_run, daemon=True)
    th.start()
    th.join(timeout)
    return box["v"] if box["done"] else default


def _reap_by_name(device, name: str, f_log) -> list[int]:
    """Kill any pre-existing process named `name` via the frida device, so a
    prior leaked (elevated) retail can't accumulate across runs."""
    killed: list[int] = []
    try:
        procs = device.enumerate_processes()
    except Exception as e:  # noqa: BLE001
        f_log.write(f"[pre-reap] enumerate failed: {e}\n")
        return killed
    nl = name.lower()
    for p in procs:
        if p.name.lower() == nl:
            try:
                device.kill(p.pid)
                killed.append(p.pid)
            except Exception as e:  # noqa: BLE001
                f_log.write(f"[pre-reap] kill {p.pid} failed: {e}\n")
    if killed:
        f_log.write(f"[pre-reap] killed stray {name}: {killed}\n")
    return killed


def _run_capture_impl(cfg: CaptureConfig, run_dir: Path) -> CaptureResult:
    agent_log = run_dir / "agent.log"
    f_log = agent_log.open("w", buffering=1)

    counts = {"hwnd": 0, "hide": 0, "msgbox": 0, "ddraw_coop": 0,
              "dsound_coop": 0, "audio_mute": 0, "display_blocked": 0,
              "fps_sample": 0, "frame": 0, "input_event": 0, "draws": 0,
              "input_replay_post": 0, "gdi_seat_present": 0}
    fps_last = {"current_fps": 0, "avg_fps": 0, "cum_secs": 0}
    done = threading.Event()

    frames_dir = run_dir / "frames"
    if cfg.frame_capture or cfg.input_segtrace_path is not None:
        frames_dir.mkdir(exist_ok=True)

    # Open the input-trace sink (one JSONL line per recorded WM_*).  We hold
    # the file open across the run; the agent emits events asynchronously
    # via on_message.
    input_trace_fp = None
    if cfg.input_record:
        input_trace_fp = (run_dir / "input_trace.jsonl").open("w", buffering=1)

    # Call-trace sink — one JSONL row per call, written as call_trace_batch
    # events arrive (batched per tick by the agent).
    call_trace_fp = None
    if cfg.call_trace:
        call_trace_fp = (run_dir / "call_trace.jsonl").open("w", buffering=1)
    counts["call_trace_calls"] = 0

    # Mem-watch sink — one JSONL row per trapped access.
    mem_watch_fp = None
    if cfg.mem_watch:
        mem_watch_fp = (run_dir / "mem_watch.jsonl").open("w", buffering=1)
    counts["mem_watch_accesses"] = 0

    # Snapshot-diff sink — one JSONL row per (region, tick) that changed.
    mem_snapshot_fp = None
    if cfg.mem_snapshot_regions:
        mem_snapshot_fp = (run_dir / "mem_snapshot.jsonl").open("w", buffering=1)
    counts["mem_snapshot_changes"] = 0

    # Anchor sink — one JSONL row per emitted {anchor,frame} (TAS framework).
    # Also kept in-memory (anchors_seen) so callers can align captures by
    # event and assert determinism across replays.
    anchor_trace_fp = None
    if cfg.anchor_trace:
        anchor_trace_fp = (run_dir / "anchors.jsonl").open("w", buffering=1)
    counts["anchor"] = 0
    anchors_seen: list[dict[str, int | str]] = []

    # Watch sink — one JSONL row per watched-global change (edge-emitted).
    watch_fp = None
    if cfg.watch:
        watch_fp = (run_dir / "watch.jsonl").open("w", buffering=1)
    counts["watch"] = 0

    # Segtrace capture map — one JSONL row per scheduled {capture} op
    # ({label, frame}); trace_studio renames frame_<tick>.bmp to the label
    # so the retail side pairs with the port's label-named captures.
    captures_fp = None
    if cfg.input_segtrace_path is not None:
        captures_fp = (run_dir / "captures.jsonl").open("w", buffering=1)

    # Preload the replay events so any parse failure surfaces before we
    # spawn the engine.  The agent expects a JSON array of {t,m,w,l}
    # objects passed via init opts; the Frida send/recv layer marshals it
    # transparently as long as everything's plain JSON-safe primitives.
    input_replay_events: list[dict[str, int]] = []
    if cfg.input_replay_path is not None:
        input_replay_events = _load_input_trace(cfg.input_replay_path)
        f_log.write(f"[input_replay] loaded {len(input_replay_events)} events "
                    f"from {cfg.input_replay_path}\n")

    # Preload the segtrace ops the same way (parse failures before spawn).
    input_segtrace_ops: list[dict[str, Any]] = []
    if cfg.input_segtrace_path is not None:
        input_segtrace_ops = _load_segtrace(cfg.input_segtrace_path)
        f_log.write(f"[input_segtrace] loaded {len(input_segtrace_ops)} ops "
                    f"from {cfg.input_segtrace_path}\n")

    def on_message(message: dict[str, Any], data: bytes | None):
        if message.get("type") == "error":
            f_log.write(f"[frida-error] {message.get('description','')} "
                        f"@ {message.get('fileName','')}:"
                        f"{message.get('lineNumber','')}\n")
            f_log.write(f"  stack: {message.get('stack','')}\n")
            return
        if message.get("type") != "send":
            return

        p = message.get("payload") or {}
        kind = p.get("kind")

        if kind == "log":
            f_log.write(f"[agent] {p.get('msg','')}\n")
            return
        if kind == "error":
            f_log.write(f"[agent-error] {p.get('where','?')}: "
                        f"{p.get('msg','')}\n")
            return
        if kind == "ready":
            f_log.write(
                f"[ready] base={p.get('base')} module={p.get('module')} "
                f"frida={p.get('fridaVersion')} "
                f"hide={p.get('hide_window')} "
                f"fwin={p.get('force_windowed')} "
                f"silent={p.get('silent_audio')} "
                f"turbo={p.get('turbo')}\n")
            return
        if kind == "hwnd_owned":
            counts["hwnd"] += 1
            f_log.write(f"[hwnd] {p.get('hwnd')} via {p.get('source')}\n")
            return
        if kind == "hwnd_owned_detail":
            f_log.write(f"[hwnd+] {p.get('hwnd')} via {p.get('api')} "
                        f"cls={p.get('cls')!r} title={p.get('title')!r}\n")
            return
        if kind == "hide_force":
            counts["hide"] += 1
            f_log.write(f"[hide] {p.get('api')} hwnd={p.get('hwnd')} "
                        f"original=0x{int(p.get('original', 0)) & 0xffffffff:08x}\n")
            return
        if kind == "messagebox_intercepted":
            counts["msgbox"] += 1
            banner = (
                "\n"
                "================================================================================\n"
                f"[!!! REDIRECTED MESSAGEBOX !!!] {p.get('api')} — "
                f"auto-returning IDOK\n"
                f"  hwnd:    {p.get('hwnd')}\n"
                f"  caption: {p.get('caption')!r}\n"
                f"  body:    {p.get('body')!r}\n"
                f"  type:    0x{int(p.get('type', 0)):x}\n"
                "================================================================================\n"
                "\n")
            f_log.write(banner)
            f_log.flush()
            print(banner, file=sys.stderr, flush=True)
            return
        if kind == "display_settings_blocked":
            counts["display_blocked"] += 1
            f_log.write(f"[disp] blocked {p.get('api')} "
                        f"flags=0x{int(p.get('flags',0)):x}\n")
            return
        if kind == "ddraw_cooplevel_rewrite":
            counts["ddraw_coop"] += 1
            f_log.write(
                f"[ddraw_coop] {p.get('iface')} hwnd={p.get('hwnd')} "
                f"original=0x{int(p.get('original',0)):x} "
                f"forced=0x{int(p.get('forced',0)):x}\n")
            return
        if kind == "dsound_cooplevel_rewrite":
            counts["dsound_coop"] += 1
            f_log.write(
                f"[dsound_coop] {p.get('iface')} hwnd={p.get('hwnd')} "
                f"original=0x{int(p.get('original',0)):x} "
                f"forced=0x{int(p.get('forced',0)):x}\n")
            return
        if kind == "audio_mute":
            counts["audio_mute"] += 1
            f_log.write(f"[mute] {p.get('api')} "
                        f"original={p.get('original')}\n")
            return
        if kind == "frame":
            if data is None:
                f_log.write("[frame] payload missing\n")
                return
            # Agent payload: [header 12 bytes][palette 1024 bytes][pixels W*H].
            try:
                w, h, fi = struct.unpack("<III", data[:12])
            except struct.error as e:
                f_log.write(f"[frame] header unpack failed: {e}\n")
                return
            palette_bytes = data[12:12 + 1024]
            pixels_off    = 12 + 1024
            expected      = pixels_off + w * h
            if len(data) < expected:
                f_log.write(f"[frame] short payload: got {len(data)} "
                            f"want {expected} (w={w} h={h})\n")
                return
            pixels = data[pixels_off:expected]
            out = frames_dir / f"frame_{fi:05d}.bmp"
            try:
                _write_paletted_bmp(out, w, h, palette_bytes, pixels)
                # Sidecar: raw 1024-byte palette (256 × PALETTEENTRY) +
                # a human-readable dump of the UI/highlight indices so we
                # can settle windowed-vs-fullscreen reserved-range behavior
                # (idx 1 = title caps, 0xf9 = selected-button highlight).
                (frames_dir / f"frame_{fi:05d}.pal").write_bytes(palette_bytes)
                # Trace Studio v3 raw payload — the agent's exact bytes
                # [w][h][idx][1024 palette][w*h 8bpp]; the native viewer's
                # container ingest reads this (index plane preserved).
                (frames_dir / f"frame_{fi:05d}.raw").write_bytes(data)
                def _pe(i):
                    o = i * 4
                    return (palette_bytes[o], palette_bytes[o+1],
                            palette_bytes[o+2], palette_bytes[o+3])
                f_log.write(f"[palette] #{fi} "
                            f"idx0x01(R,G,B,fl)={_pe(0x01)} "
                            f"idx0x3f={_pe(0x3f)} "
                            f"idx0xf9={_pe(0xf9)} "
                            f"idx0xff={_pe(0xff)}\n")
                counts["frame"] += 1
                f_log.write(f"[frame] #{fi} {w}x{h} -> {out.name} "
                            f"virt_ms={p.get('virtual_now_ms')} "
                            f"t_ms={p.get('t_ms')}\n")
            except Exception as e:
                f_log.write(f"[frame] write failed {out}: {e}\n")
            return
        if kind == "draws":
            # Trace Studio v3 draw-step sidecar — the OLDR payload (base plane +
            # ordered op-deltas) the agent's draw-capture CModule serialized for
            # this frame. Pairs with frame_<idx>.raw by index.
            if data is None:
                f_log.write("[draws] payload missing\n")
                return
            fi = int(p.get("frame_idx", 0))
            try:
                (frames_dir / f"frame_{fi:05d}.draws").write_bytes(data)
                counts["draws"] = counts.get("draws", 0) + 1
                f_log.write(f"[draws] #{fi} {len(data)} bytes "
                            f"({p.get('n_draws')} draws)\n")
            except Exception as e:
                f_log.write(f"[draws] write failed #{fi}: {e}\n")
            return
        if kind == "globals_dump":
            regions = p.get("regions") or []
            rows = []
            for r in regions:
                bts = r.get("bytes")
                hexs = "".join(f"{b & 0xff:02x}" for b in bts) if bts else None
                rows.append({"va": r.get("va"), "size": r.get("size"),
                             "label": r.get("label"), "hex": hexs})
                f_log.write(f"[globals] {r.get('label')} "
                            f"@0x{(r.get('va') or 0):x} ({r.get('size')}B) = "
                            f"{hexs}\n")
            try:
                (run_dir / "globals_dump.json").write_text(
                    json.dumps(rows, indent=2))
            except Exception as e:
                f_log.write(f"[globals] write failed: {e}\n")
            return
        if kind == "globals_blob":
            label = p.get("label") or f"0x{(p.get('va') or 0):x}"
            ok = bool(p.get("ok")) and data is not None
            dump_dir = run_dir / "dump"
            dump_dir.mkdir(exist_ok=True)
            if ok:
                out = dump_dir / f"{label}.bin"
                try:
                    out.write_bytes(data)
                    counts["globals_blob"] = counts.get("globals_blob", 0) + 1
                    f_log.write(
                        f"[blob] {label} @0x{(p.get('va') or 0):x}"
                        f"{' deref→' + str(p.get('resolved')) if p.get('deref') else ''}"
                        f" size={p.get('size')} -> {out.name} "
                        f"({len(data)}B)\n")
                except Exception as e:
                    f_log.write(f"[blob] write failed {label}: {e}\n")
            else:
                f_log.write(f"[blob] {label} read FAILED (ok={p.get('ok')}, "
                            f"data={'present' if data else 'none'})\n")
            return
        if kind == "fps_sample":
            counts["fps_sample"] += 1
            fps_last["current_fps"] = int(p.get("current_fps", 0))
            fps_last["avg_fps"]     = int(p.get("avg_fps", 0))
            fps_last["cum_secs"]    = int(p.get("cumulative_seconds", 0))
            f_log.write(
                f"[fps] current={p.get('current_fps')} "
                f"avg={p.get('avg_fps')} "
                f"cum_secs={p.get('cumulative_seconds')} "
                f"virt_ms={p.get('virtual_now_ms')} "
                f"t_ms={p.get('t_ms')}\n")
            return
        if kind == "input_event":
            counts["input_event"] += 1
            ev = {"t": int(p.get("t", 0)),
                  "m": int(p.get("m", 0)),
                  "w": int(p.get("w", 0)),
                  "l": int(p.get("l", 0))}
            if input_trace_fp is not None:
                input_trace_fp.write(json.dumps(ev) + "\n")
            f_log.write(
                f"[input] t={ev['t']} m=0x{ev['m']:x} "
                f"w=0x{ev['w']:x} l=0x{ev['l']:x} "
                f"hwnd={p.get('hwnd')}\n")
            return
        if kind == "input_replay_post":
            counts["input_replay_post"] += 1
            f_log.write(
                f"[replay] idx={p.get('idx')} t={p.get('t')} "
                f"tick_now={p.get('tick_now')} "
                f"m=0x{int(p.get('m', 0)):x} "
                f"w=0x{int(p.get('w', 0)):x} "
                f"l=0x{int(p.get('l', 0)):x}\n")
            return
        if kind == "anchor":
            counts["anchor"] += 1
            name = str(p.get("anchor", "?"))
            frame = int(p.get("frame", 0))
            rec = {"anchor": name, "frame": frame}
            anchors_seen.append(rec)
            if anchor_trace_fp is not None:
                anchor_trace_fp.write(json.dumps(rec) + "\n")
            f_log.write(f"[anchor] {name} @ frame {frame}\n")
            return
        if kind == "segtrace_wait":
            f_log.write(f"[segtrace] wait {p.get('anchor')} resolved → "
                        f"rebased segment-0 to frame {p.get('frame')}\n")
            return
        if kind == "segtrace_wait_until":
            f_log.write(f"[segtrace] wait_until va=0x{int(p.get('va',0)):x} "
                        f"val={p.get('val')} → rebased to frame {p.get('frame')}\n")
            return
        if kind == "segtrace_capture_sched":
            f_log.write(f"[segtrace] capture scheduled @ frame {p.get('at')} "
                        f"(n={p.get('n')} label={p.get('label')!r})\n")
            if captures_fp is not None:
                captures_fp.write(json.dumps(
                    {"label": p.get("label") or "capture",
                     "frame": int(p.get("at", 0))}) + "\n")
            return
        if kind == "state_dump":
            frame = int(p.get("frame", 0))
            sdir = run_dir / "state"
            sdir.mkdir(exist_ok=True)
            (sdir / f"state_{frame:05d}.json").write_text(
                json.dumps({"slots": p.get("slots") or [],
                            "sub": p.get("sub") or {}}) + "\n")
            f_log.write(f"[state_dump] frame {frame} "
                        f"({len(p.get('slots') or [])} slots)\n")
            return
        if kind == "segtrace_done":
            f_log.write(f"[segtrace] done @ frame {p.get('frame')} — "
                        f"all ops consumed, captures taken\n")
            done.set()
            return
        if kind == "watch":
            counts["watch"] += 1
            name = str(p.get("name", "?"))
            frame = int(p.get("frame", 0))
            val = p.get("val")
            rec = {"name": name, "frame": frame, "val": val}
            if watch_fp is not None:
                watch_fp.write(json.dumps(rec) + "\n")
            f_log.write(f"[watch] {name} = {val} @ frame {frame}\n")
            return
        if kind == "gdi_seat_present":
            counts["gdi_seat_present"] += 1
            f_log.write(
                f"[gdi_seat] present #{p.get('count')} t_ms={p.get('t_ms')}\n")
            return
        if kind == "agent_show_window":
            f_log.write(
                f"[show_window] hwnd={p.get('hwnd')} "
                f"after_ms={p.get('after_ms')} t_ms={p.get('t_ms')}\n")
            return
        if kind == "call_trace_hooked":
            f_log.write(
                f"[call_trace] hooked n_ok={p.get('n_ok')} "
                f"n_fail={p.get('n_fail')} of {p.get('n_req')} VAs\n")
            return
        if kind == "call_trace_batch":
            # Flatten the agent's per-tick batch into one JSONL row per
            # call, matching src/call_trace.c's schema so call_trace_diff.py
            # reads both sides identically.
            evs = p.get("events", []) or []
            counts["call_trace_calls"] += len(evs)
            if call_trace_fp is not None:
                for ev in evs:
                    call_trace_fp.write(json.dumps({
                        "va":     int(ev.get("va", 0)),
                        "ret_va": int(ev.get("ret_va", 0)),
                        # Per-event frame (stamped at call time by the agent);
                        # fall back to the batch frame for older traces.
                        "frame":  int(ev.get("frame", p.get("frame", 0))),
                        "ts":     int(ev.get("ts", 0)),
                    }) + "\n")
            f_log.write(
                f"[call_trace] frame={p.get('frame')} "
                f"calls={len(evs)}\n")
            return
        if kind == "mem_watch_ready":
            f_log.write(f"[mem_watch] armed precise={p.get('precise')} "
                        f"regions={p.get('regions')}\n")
            return
        if kind == "mem_access_batch":
            evs = p.get("events", []) or []
            counts["mem_watch_accesses"] += len(evs)
            if mem_watch_fp is not None:
                for ev in evs:
                    mem_watch_fp.write(json.dumps({
                        "op":     ev.get("op", ""),
                        "from":   int(ev.get("from", 0)),
                        "addr":   int(ev.get("addr", 0)),
                        "region": int(ev.get("region", 0)),
                        "label":  ev.get("label", ""),
                        "frame":  int(p.get("frame", 0)),
                        "ts":     int(ev.get("ts", 0)),
                    }) + "\n")
            f_log.write(f"[mem_watch] frame={p.get('frame')} "
                        f"accesses={len(evs)}\n")
            return
        if kind == "mem_snapshot":
            evs = p.get("events", []) or []
            counts["mem_snapshot_changes"] += len(evs)
            if mem_snapshot_fp is not None:
                for ev in evs:
                    # changes: [[off, old, new], ...]
                    mem_snapshot_fp.write(json.dumps({
                        "region": int(ev.get("region", 0)),
                        "label":  ev.get("label", ""),
                        "va":     int(ev.get("va", 0)),
                        "tick":   int(ev.get("tick", 0)),
                        "n":      int(ev.get("n", 0)),
                        "changes": ev.get("changes", []),
                    }) + "\n")
            for ev in evs:
                f_log.write(f"[mem_snapshot] tick={ev.get('tick')} "
                            f"region={ev.get('label')} "
                            f"changed={ev.get('n')} bytes\n")
            return
        # unknown kind — log it raw so we can spot agent extensions we
        # haven't wired up yet.
        f_log.write(f"[unknown-event] {json.dumps(p)}\n")

    # ─── ensure frida-server is up ───
    if cfg.auto_start_server:
        ok = ensure_frida_server(cfg.remote, cfg.server_exe)
        if not ok:
            f_log.write(f"[fatal] could not reach frida-server at {cfg.remote}\n")
            f_log.close()
            return CaptureResult(exit_code=125, elapsed_ms=0)

    # ─── translate exe + cwd to Windows paths ───
    win_exe = wslpath_w(cfg.exe.resolve())
    win_cwd = wslpath_w(cfg.cwd.resolve())
    argv = [win_exe, *cfg.exe_args]

    device = frida.get_device_manager().add_remote_device(cfg.remote)

    # Pre-reap any leftover (elevated) retail from a prior crashed run before
    # spawning a fresh one — so leaks can't accumulate across runs.
    _reap_by_name(device, cfg.exe.name, f_log)

    session = None
    script  = None
    pid     = None
    exit_code = 0

    pidfile = run_dir / "spawned.pid"
    # Heartbeat file the detached reaper watches; touched by the heartbeat
    # thread, deleted on clean teardown.  Its staleness = "controller died".
    heartbeat_file = run_dir / "controller.alive"
    teardown_done = threading.Event()
    # Mutable holder so the watchdog/heartbeat threads see the pid once spawned.
    wd = {"pid": None}
    reaper_proc = None

    # Controller-side watchdog: if a clean teardown hasn't completed within the
    # run duration + grace (e.g. a fragile frida RPC wedged the main thread),
    # force-kill the target through the device and hard-exit.  Guarantees we
    # neither leak an elevated retail nor hang the shell.
    def _watchdog():
        budget = (cfg.duration_ms / 1000.0) + WATCHDOG_GRACE_S
        if teardown_done.wait(timeout=budget):
            return  # clean teardown finished in time
        p = wd["pid"]
        if p is not None:
            try: device.kill(p)
            except Exception: pass  # noqa: BLE001
        try:
            f_log.write("[watchdog] teardown exceeded budget "
                        f"({budget:.0f}s) — emergency kill pid={p} + exit\n")
            f_log.flush()
        except Exception: pass  # noqa: BLE001
        os._exit(124)

    # Controller→agent heartbeat: the in-target watchdog self-terminates retail
    # if these stop arriving (covers the controller being SIGKILLed, where no
    # finally and no controller watchdog can run).
    def _heartbeat():
        while not teardown_done.is_set():
            # Touch the file the detached reaper watches (its staleness is how
            # the reaper detects controller death) AND ping the in-target
            # agent watchdog.
            try: heartbeat_file.touch()
            except Exception: pass  # noqa: BLE001
            s = script  # captured by closure; may still be None very early
            if s is not None:
                try: s.exports_sync.heartbeat()
                except Exception: pass  # noqa: BLE001
            teardown_done.wait(timeout=HEARTBEAT_INTERVAL_S)

    # Convert SIGTERM/SIGINT into normal Python exits so the finally block
    # runs and we don't leak orphan retail Lords2.exe processes.
    def _signal_exit(signum, _frame):  # noqa: ARG001
        sys.exit(128 + signum)
    prev_term = signal.signal(signal.SIGTERM, _signal_exit)
    prev_int  = signal.signal(signal.SIGINT,  _signal_exit)

    t0 = time.monotonic()
    try:
        f_log.write(f"[spawn] argv={argv} cwd={win_cwd}\n")
        pid = device.spawn(argv, cwd=win_cwd)
        session = device.attach(pid)
        script = session.create_script(AGENT_JS.read_text())
        script.on("message", on_message)
        script.load()

        init_cfg = {
            "module":                    cfg.exe.name,
            "hide_window":               bool(cfg.hide_window),
            "silent_audio":              bool(cfg.silent_audio),
            "force_windowed":            bool(cfg.force_windowed),
            "keep_retail_palette":       bool(cfg.keep_retail_palette),
            "turbo":                     bool(cfg.turbo),
            "turbo_step_ms":             int(cfg.turbo_step_ms),
            "gdi_seat":                  bool(cfg.gdi_seat),
            "agent_present":             bool(cfg.agent_present),
            "force_active":              bool(cfg.force_active),
            "agent_present_every_ticks": int(cfg.agent_present_every_ticks),
            "show_window_after_ms":      int(cfg.show_window_after_ms),
            "show_window_client_w":      int(cfg.show_window_client_w),
            "show_window_client_h":      int(cfg.show_window_client_h),
            "frame_capture":             bool(cfg.frame_capture),
            "frame_capture_max":         int(cfg.frame_capture_max),
            "frame_capture_warmup_secs": int(cfg.frame_capture_warmup_secs),
            "dump_globals":              cfg.dump_globals or [],
            "dump_regions":              cfg.dump_regions or [],
            "poke_globals":              cfg.poke_globals or [],
            "skip_intro":                bool(cfg.skip_intro),
            "input_record":              bool(cfg.input_record),
            "input_replay":              input_replay_events,
            "input_segtrace":            input_segtrace_ops,
            "state_dump_slots":          cfg.state_dump_slots or [],
            "call_trace":                bool(cfg.call_trace),
            "call_trace_vas":            [int(v) for v in cfg.call_trace_vas],
            "call_trace_frames":         [int(v) for v in cfg.call_trace_frames],
            "call_trace_min_cum_secs":   int(cfg.call_trace_min_cum_secs),
            "call_trace_max_frames":     int(cfg.call_trace_max_frames),
            "mem_watch":                 bool(cfg.mem_watch),
            "mem_watch_regions":         cfg.mem_watch_regions,
            "mem_watch_precise":         bool(cfg.mem_watch_precise),
            "mem_watch_rearm_cap":       int(cfg.mem_watch_rearm_cap),
            "mem_watch_arm_between":     cfg.mem_watch_arm_between,
            "mem_snapshot_regions":      cfg.mem_snapshot_regions,
            "anchor_trace":              bool(cfg.anchor_trace),
            "capture_draws":             bool(cfg.capture_draws),
            "capture_force_redraw":      bool(cfg.capture_force_redraw),
            "suppress_edge_scroll":      bool(cfg.suppress_edge_scroll),
            "save_sandbox":              cfg.save_sandbox or "",
            "watch":                     cfg.watch or [],
            "rng_seed":                  cfg.rng_seed,
            "heartbeat_timeout_ms":      HEARTBEAT_TIMEOUT_MS,
        }
        script.exports_sync.init(init_cfg)
        device.resume(pid)

        # Record the spawned pid + arm the leak-proofing threads now that the
        # target is running.
        wd["pid"] = pid
        try: pidfile.write_text(str(pid))
        except Exception: pass  # noqa: BLE001
        try: heartbeat_file.touch()
        except Exception: pass  # noqa: BLE001
        threading.Thread(target=_watchdog,  daemon=True).start()
        threading.Thread(target=_heartbeat, daemon=True).start()

        # Detached reaper — survives even a SIGKILL of this controller (own
        # session, not our process group) and reaps the elevated retail if our
        # heartbeat file goes stale.  This is the only layer that covers the
        # controller dying without running any of its own cleanup.
        try:
            reaper_proc = subprocess.Popen(
                [sys.executable, str(ROOT / "tools" / "reaper.py"),
                 "--remote", cfg.remote, "--pid", str(pid),
                 "--name", cfg.exe.name,
                 "--heartbeat-file", str(heartbeat_file),
                 "--log", str(run_dir / "reaper.log"),
                 "--stale-s", "15",
                 "--max-life-s", str(cfg.duration_ms / 1000.0 + 120.0)],
                start_new_session=True,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            f_log.write(f"[reaper] detached reaper spawned pid={reaper_proc.pid}\n")
        except Exception as e:  # noqa: BLE001
            f_log.write(f"[reaper] failed to spawn detached reaper: {e}\n")

        deadline = t0 + (cfg.duration_ms / 1000.0)
        while not done.is_set() and time.monotonic() < deadline:
            time.sleep(0.05)

        elapsed_ms = int((time.monotonic() - t0) * 1000)
    finally:
        # Every fragile frida call here is bounded by a timeout so a wedged
        # RPC can't prevent the kill (the bug that leaked two elevated retail
        # processes: an un-timeout'd get_gdi_seat_stats() hung *before* the
        # kill).  Diagnostics are best-effort; killing the target is not.

        # Drain the mem-watch buffer (final partial batch) before kill,
        # bounded so a wedged agent can't block teardown.
        if script is not None and cfg.mem_watch:
            st = _call_with_timeout(
                lambda: script.exports_sync.get_mem_watch_status(),
                TEARDOWN_CALL_TIMEOUT_S)
            if st is not None:
                f_log.write(f"[mem_watch_status] {st}\n")

        # Diagnostics first (bounded), while the agent is still attached.
        if script is not None and cfg.gdi_seat:
            stats = _call_with_timeout(
                lambda: script.exports_sync.get_gdi_seat_stats(),
                TEARDOWN_CALL_TIMEOUT_S)
            if stats is not None:
                f_log.write(f"[gdi_seat_stats] {stats}\n")
                counts["gdi_seat_observed_blts"] = int(stats.get("blt_observed", 0))
                counts["gdi_seat_skipped_fill"]  = int(stats.get("blt_skipped_fill", 0))
                counts["gdi_seat_skipped_other"] = int(stats.get("blt_skipped_other", 0))
                counts["gdi_seat_patched"]       = bool(stats.get("patched", False))
            else:
                f_log.write("[gdi_seat_stats] fetch timed out/failed\n")

        # KILL — the one step that must always happen.  Bounded; retried via
        # a fresh enumerate-by-name pass if the direct pid kill doesn't land
        # (e.g. the pid is mid-respawn).  Routed through the device because
        # the elevated retail is unkillable from non-admin WSL.
        if pid is not None:
            ok = _call_with_timeout(
                lambda: (device.kill(pid), True)[1], TEARDOWN_CALL_TIMEOUT_S,
                default=False)
            if not ok:
                f_log.write(f"[shutdown] direct kill pid={pid} timed out; "
                            f"reaping by name\n")
                killed = _call_with_timeout(
                    lambda: _reap_by_name(device, cfg.exe.name, f_log),
                    TEARDOWN_CALL_TIMEOUT_S, default=[])
                if not killed:
                    f_log.write(f"[shutdown] could not confirm kill of "
                                f"pid={pid}\n")
                    exit_code = 1

        # Cleanup of the attach plumbing — best-effort, bounded, AFTER the
        # kill so a hang here can never leak the target.
        if script is not None:
            _call_with_timeout(lambda: script.unload(), TEARDOWN_CALL_TIMEOUT_S)
        if session is not None:
            _call_with_timeout(lambda: session.detach(), TEARDOWN_CALL_TIMEOUT_S)

        try: pidfile.unlink()
        except Exception: pass  # noqa: BLE001
        # Delete the heartbeat file BEFORE signalling threads — its absence
        # tells the detached reaper this was a clean teardown (don't kill).
        try: heartbeat_file.unlink()
        except Exception: pass  # noqa: BLE001
        # Signal the watchdog + heartbeat threads that teardown is complete.
        teardown_done.set()
        # Reaper should self-exit on the missing heartbeat file; nudge it too.
        if reaper_proc is not None:
            try: reaper_proc.terminate()
            except Exception: pass  # noqa: BLE001
        signal.signal(signal.SIGTERM, prev_term)
        signal.signal(signal.SIGINT,  prev_int)

    elapsed_ms = int((time.monotonic() - t0) * 1000)
    f_log.write(f"[summary] {counts}\n")
    f_log.close()
    if input_trace_fp is not None:
        input_trace_fp.close()
    if call_trace_fp is not None:
        call_trace_fp.close()
    if mem_watch_fp is not None:
        mem_watch_fp.close()
    if mem_snapshot_fp is not None:
        mem_snapshot_fp.close()
    if anchor_trace_fp is not None:
        anchor_trace_fp.close()
    if watch_fp is not None:
        watch_fp.close()
    if captures_fp is not None:
        captures_fp.close()

    return CaptureResult(
        exit_code=exit_code,
        elapsed_ms=elapsed_ms,
        hwnd_count=counts["hwnd"],
        hide_count=counts["hide"],
        msgbox_count=counts["msgbox"],
        cooplevel_count=counts["ddraw_coop"] + counts["dsound_coop"],
        audio_mute_count=counts["audio_mute"],
        fps_sample_count=counts["fps_sample"],
        last_current_fps=fps_last["current_fps"],
        last_avg_fps=fps_last["avg_fps"],
        last_cum_seconds=fps_last["cum_secs"],
        frame_count=counts["frame"],
        input_event_count=counts["input_event"],
        input_replay_post_count=counts["input_replay_post"],
        gdi_seat_present_count=counts["gdi_seat_present"],
        call_trace_calls=counts.get("call_trace_calls", 0),
        anchor_count=counts.get("anchor", 0),
        anchors=anchors_seen,
    )


# ─── cli ──────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--remote", default=DEFAULT_REMOTE,
                    help=f"frida-server endpoint (default: {DEFAULT_REMOTE})")
    ap.add_argument("--duration-ms", type=int, default=10_000,
                    help="how long to let the engine run before tearing down")
    ap.add_argument("--run-id", default=None,
                    help="run directory name under runs/ (default: timestamp)")
    ap.add_argument("--exe",
                    default=str(RETAIL_EXE),
                    help="path to the .exe to spawn")
    ap.add_argument("--cwd",
                    default=str(ASSET_CWD),
                    help="working directory for the spawned exe")
    ap.add_argument("--no-hide", action="store_true")
    ap.add_argument("--no-silent-audio", action="store_true")
    ap.add_argument("--no-force-windowed", action="store_true")
    ap.add_argument("--force-8bpp-palette", action="store_true",
                    help="force retail's 8bpp blackout palette builder "
                         "(FUN_0042f449: Windows-reserved slots black, so the "
                         "title caps + selected-button hover highlight render "
                         "black).  This is the OLD default; it matches the 8bpp "
                         "windowed groundtruths but NOT what a real player sees. "
                         "Default (without this flag) builds the full-color "
                         "palette (FUN_0042f306: red caps + red hover) that "
                         "matches real fullscreen retail.  Only affects "
                         "--force-windowed runs.")
    ap.add_argument("--no-gdi-seat", action="store_true",
                    help="don't reroute retail's primary-surface Blt "
                         "through GDI StretchDIBits.  Disables visual "
                         "rendering under DWM in windowed mode (debug aid).")
    ap.add_argument("--agent-present", action="store_true",
                    help="agent-driven continuous GDI present every N "
                         "engine_ticks.  Interactive verification only — "
                         "lets you watch the engine render during "
                         "stretches where retail's own present pipeline "
                         "is dormant (Smacker intro, etc).")
    ap.add_argument("--agent-present-every-ticks", type=int, default=100,
                    help="when --agent-present is set, drive a present "
                         "every N engine_ticks (default 100 ≈ 20 Hz at "
                         "default turbo step)")
    ap.add_argument("--force-active", action="store_true",
                    help="poke the main-loop present gate "
                         "(var_app_active=1 / var_surface_lost=0) every "
                         "tick so the engine runs full-speed + services "
                         "input even though a frida-spawned window never "
                         "wins foreground (else it Sleep(200)s and feels "
                         "frozen).  Implied by --interactive.")
    ap.add_argument("--interactive", action="store_true",
                    help="convenience for a live, real-mouse retail seat: "
                         "turns on --agent-present + --force-active, shows "
                         "the window after 2 s (if --show-window-after-ms "
                         "is 0), and keeps the session alive for 5 min (if "
                         "--duration-ms is left at its default).  Combine "
                         "with --skip-intro and an --input-trace to auto-"
                         "navigate to a screen, then drive it by hand.")
    ap.add_argument("--show-window-after-ms", type=int, default=0,
                    help="programmatically ShowWindow(SW_SHOW) on the "
                         "engine's main window once this many real-ms "
                         "have elapsed since init.  0 = never auto-show. "
                         "Pairs with --agent-present and hide_window=on "
                         "(default) for interactive visual verification.")
    ap.add_argument("--show-window-client-w", type=int, default=640,
                    help="client-area width to resize the engine's "
                         "window to after --show-window-after-ms.  "
                         "Default 640 (engine's native render res — "
                         "1:1 pixel-perfect).")
    ap.add_argument("--show-window-client-h", type=int, default=480,
                    help="client-area height to resize the engine's "
                         "window to after --show-window-after-ms.  "
                         "Default 480.")
    ap.add_argument("--turbo", action="store_true",
                    help="enable Sleep no-op (v1) + virtual timeGetTime (v2)")
    ap.add_argument("--turbo-step-ms", type=int, default=17,
                    help="virtual-ms advanced per per-tick driver iteration "
                         "when --turbo is set (default 17 ≈ 58 ticks/s perceived)")
    ap.add_argument("--frame-capture", action="store_true",
                    help="grab the DDraw back surface once per engine "
                         "cumulative-seconds rollover; writes one BMP per "
                         "capture under <run_dir>/frames/")
    ap.add_argument("--frame-capture-max", type=int, default=16,
                    help="stop frame capture after this many frames "
                         "(default 16, 0 = unlimited)")
    ap.add_argument("--frame-capture-warmup-secs", type=int, default=5,
                    help="skip captures for the first N engine "
                         "cumulative-seconds rollovers so the engine has "
                         "time to render a first frame (default 5)")
    ap.add_argument("--dump-globals", default=None,
                    help="comma-separated list of VA:SIZE[:LABEL] specs "
                         "(VA is a Ghidra VA, ImageBase 0x00400000) to read "
                         "once at the first captured frame; written to "
                         "<run_dir>/globals_dump.json.  Seeds a parity port's "
                         "state from retail's exact values.")
    ap.add_argument("--dump-regions", default=None,
                    help="comma-separated list of [*]VA:SIZE[:LABEL] specs to "
                         "read once at the first captured frame, each written "
                         "as raw bytes to <run_dir>/dump/<label>.bin.  A "
                         "leading '*' dereferences the 32-bit pointer at VA "
                         "first (heap sprite-bank base) then reads SIZE bytes "
                         "from the target.  For large arrays/blobs that would "
                         "be wasteful as JSON (cf. --dump-globals for scalars).")
    ap.add_argument("--poke-globals", default=None,
                    help="comma-separated list of VA:HEXBYTES specs (VA is a "
                         "Ghidra VA, HEXBYTES is a hex string like 41424300) "
                         "written into engine memory every tick to force "
                         "scenario state before capture.")
    ap.add_argument("--skip-intro", action="store_true",
                    help="skip the boot intro movie chain (intro/imptitle/"
                         "credits): the agent snaps each Smacker movie to its "
                         "last frame so the engine reaches the title menu "
                         "(state 0x1f) in ~3 frames instead of the ~23 real-sec "
                         "turbo fast-forward; pair with a small "
                         "--frame-capture-warmup-secs")
    ap.add_argument("--input-record", action="store_true",
                    help="record every dispatched WM_KEY*/WM_CHAR/WM_MOUSE* "
                         "event to <run_dir>/input_trace.jsonl, stamped with "
                         "the engine_tick count at moment of dispatch")
    ap.add_argument("--input-trace",
                    default=None,
                    help="replay a JSONL trace previously captured with "
                         "--input-record: PostMessageA each event to the "
                         "engine's main HWND when its recorded tick stamp "
                         "is reached")
    ap.add_argument("--call-trace", action="store_true",
                    help="hook the VAs in --call-trace-vas on retail and emit "
                         "per-tick call_trace_batch events to "
                         "<run_dir>/call_trace.jsonl (diff with call_trace_diff.py)")
    ap.add_argument("--call-trace-vas", default=None,
                    help="comma-separated Ghidra VAs to hook (hex 0x.. or "
                         "decimal), OR @PATH to a JSON file containing a list "
                         "of VAs (e.g. from gen_call_trace_probes.py)")
    ap.add_argument("--call-trace-frames", default=None,
                    help="comma-separated sim-frame (g_tick_count) filter; "
                         "omit to emit every frame")
    ap.add_argument("--call-trace-min-cum-secs", type=int, default=0,
                    help="emit call-trace once the engine reaches this "
                         "cumulative-second (the target scene under turbo), "
                         "for --call-trace-max-frames ticks (0 = tick filter)")
    ap.add_argument("--call-trace-max-frames", type=int, default=8,
                    help="number of ticks to emit in cum-sec window mode")
    ap.add_argument("--input-segtrace",
                    default=None,
                    help="replay an anchor-segmented input trace (TAS "
                         "framework): a JSONL superset of --input-trace with "
                         "{wait:ANCHOR} / {wait_until:{...}} segment breaks and "
                         "{capture:N} anchor-relative screenshots. Within a "
                         "segment, frame stamps are relative to the opening "
                         "anchor. See docs/plans/tas-framework.md")
    ap.add_argument("--state-dump-slots", default=None,
                    help="save-schema slot list (JSON file: [{i,va,size}]) — "
                         "dump these at every segtrace {capture} instant to "
                         "state/state_<tick>.json (trace_studio state parity)")
    ap.add_argument("--capture-draws", action="store_true",
                    help="Trace Studio v3 draw-stepping: capture per-draw "
                         "write-deltas at {capture} ticks (CModule diff vs the "
                         "back buffer) → frames/frame_<idx>.draws (OLDR sidecar)")
    ap.add_argument("--capture-force-redraw", action="store_true",
                    help="force a full strategic re-render on each {capture} tick "
                         "(DAT_0057d340=2) so the map composites into the draw journal")
    ap.add_argument("--suppress-edge-scroll", action="store_true",
                    help="neuter retail's strat-map mouse edge-scroll (FUN_00432221) "
                         "so the captured camera is the loaded-save scroll, not a "
                         "host-cursor screen-edge scroll (parity determinism)")
    ap.add_argument("--save-sandbox", default=None,
                    help="Windows path to a per-run sandbox dir; the agent "
                         "redirects every save-file open (.sav family + "
                         "Castles.dat) into it so the replay never reads or "
                         "writes the real game-dir saves (reproducibility). "
                         "Pre-seed it with the reference save.")
    ap.add_argument("--anchor-trace", action="store_true",
                    help="emit deterministic anchors ({anchor,frame} on "
                         "engine-state / screen-id transitions) to "
                         "<run_dir>/anchors.jsonl (TAS framework; see "
                         "docs/plans/tas-framework.md)")
    ap.add_argument("--watch", action="append", default=[],
                    metavar="NAME=0xVA:TYPE",
                    help="watch a global and emit {name,frame,val} on every "
                         "change → <run_dir>/watch.jsonl. TYPE in "
                         "u8/i8/u16/i16/u32/i32/f32. Repeatable. The state "
                         "oracle for authoring wait_until predicates.")
    ap.add_argument("--rng-seed", nargs="?", const="canonical", default=None,
                    metavar="A[,B[,SINGLE]]",
                    help="pin the RNG seed each pre-ingame tick so the new-game "
                         "map is reproducible across targets (retail/port don't "
                         "seed from the same wall-clock). Bare flag = retail-"
                         "canonical seeds; or pass A[,B[,SINGLE]] hex/dec "
                         "states. Use for any port↔retail comparison capture.")
    ap.add_argument("--exe-arg", action="append", default=[],
                    help="argument to pass to the spawned exe (repeatable)")
    args = ap.parse_args(argv)

    def _parse_va_list(spec: str | None) -> list[int]:
        if not spec:
            return []
        if spec.startswith("@"):
            data = json.loads(Path(spec[1:]).read_text())
            # Accept either a bare list of VAs or {"vas":[...]}.
            items = data["vas"] if isinstance(data, dict) else data
            return [int(v, 0) if isinstance(v, str) else int(v) for v in items]
        return [int(t, 0) for t in spec.split(",") if t.strip()]

    call_trace_vas    = _parse_va_list(args.call_trace_vas)
    call_trace_frames = ([int(t) for t in args.call_trace_frames.split(",")
                          if t.strip()] if args.call_trace_frames else [])

    def _parse_dump_globals(spec: str | None) -> list[dict[str, Any]]:
        """VA:SIZE[:LABEL],...  → [{va, size, label}].  VA accepts 0x hex."""
        out: list[dict[str, Any]] = []
        if not spec:
            return out
        for tok in spec.split(","):
            tok = tok.strip()
            if not tok:
                continue
            parts = tok.split(":")
            va = int(parts[0], 0)
            size = int(parts[1], 0) if len(parts) > 1 else 1
            label = parts[2] if len(parts) > 2 else f"0x{va:x}"
            out.append({"va": va, "size": size, "label": label})
        return out

    dump_globals = _parse_dump_globals(args.dump_globals)

    def _parse_dump_regions(spec: str | None) -> list[dict[str, Any]]:
        """[*]VA:SIZE[:LABEL],...  → [{va, size, label, deref}].  A leading
        '*' marks the VA as a pointer to dereference before reading."""
        out: list[dict[str, Any]] = []
        if not spec:
            return out
        for tok in spec.split(","):
            tok = tok.strip()
            if not tok:
                continue
            deref = tok.startswith("*")
            if deref:
                tok = tok[1:]
            parts = tok.split(":")
            va = int(parts[0], 0)
            size = int(parts[1], 0) if len(parts) > 1 else 1
            label = parts[2] if len(parts) > 2 else f"0x{va:x}"
            out.append({"va": va, "size": size, "label": label,
                        "deref": deref})
        return out

    dump_regions = _parse_dump_regions(args.dump_regions)

    def _parse_poke_globals(spec: str | None) -> list[dict[str, Any]]:
        """VA:HEXBYTES,...  → [{va, bytes}].  HEXBYTES is a hex string."""
        out: list[dict[str, Any]] = []
        if not spec:
            return out
        for tok in spec.split(","):
            tok = tok.strip()
            if not tok:
                continue
            va_s, _, hex_s = tok.partition(":")
            va = int(va_s, 0)
            hx = hex_s.strip()
            out.append({"va": va, "bytes": list(bytes.fromhex(hx))})
        return out

    poke_globals = _parse_poke_globals(args.poke_globals)

    def _parse_watch(specs: list[str]) -> list[dict]:
        out: list[dict] = []
        for spec in specs:
            # NAME=0xVA:TYPE
            name, _, rest = spec.partition("=")
            va_s, _, ty = rest.partition(":")
            if not name or not va_s:
                raise SystemExit(f"--watch: bad spec {spec!r} "
                                 f"(want NAME=0xVA:TYPE)")
            out.append({"name": name, "va": int(va_s, 0),
                        "type": (ty or "i32")})
        return out

    def _parse_rng_seed(spec: str | None) -> dict | None:
        if spec is None:
            return None
        if spec == "canonical":
            return {}                      # agent uses its retail-canonical defaults
        keys = ("a", "b", "single")
        out: dict = {}
        for k, tok in zip(keys, spec.split(",")):
            tok = tok.strip()
            if tok:
                out[k] = int(tok, 0) & 0xffffffff
        return out

    watch_specs = _parse_watch(args.watch)

    state_dump_slots: list[dict] = []
    if args.state_dump_slots:
        rows = json.loads(Path(args.state_dump_slots).read_text())
        state_dump_slots = [{"i": int(r["i"]), "va": int(r["va"]),
                             "size": int(r["size"])} for r in rows]
    rng_seed    = _parse_rng_seed(args.rng_seed)

    run_id = args.run_id or dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = ROOT / "runs" / "capture" / run_id
    run_dir.mkdir(parents=True, exist_ok=True)

    # --interactive: a live real-mouse seat.  Turn on the present + active
    # seats, show the window early (unless explicitly set), and keep the
    # session alive long enough to play (unless --duration-ms was set).
    eff_agent_present     = args.agent_present
    eff_force_active      = args.force_active
    eff_show_window_after = args.show_window_after_ms
    eff_duration_ms       = args.duration_ms
    eff_present_every     = args.agent_present_every_ticks
    if args.interactive:
        eff_agent_present = True
        eff_force_active  = True
        if eff_show_window_after <= 0:
            eff_show_window_after = 2000
        if eff_duration_ms == 10_000:       # argparse default → bump
            eff_duration_ms = 300_000
        if eff_present_every == 100:        # argparse default → tighten
            eff_present_every = 3           # smooth GDI present at full tick rate
    # A segtrace ends the run early on segtrace_done; give it generous headroom
    # so a late anchor (e.g. MAP_FREEROAM after a real load) is still reached.
    if args.input_segtrace and eff_duration_ms == 10_000:
        eff_duration_ms = 300_000

    cfg = CaptureConfig(
        duration_ms=eff_duration_ms,
        remote=args.remote,
        exe=Path(args.exe).resolve(),
        cwd=Path(args.cwd).resolve(),
        hide_window=not args.no_hide,
        silent_audio=not args.no_silent_audio,
        force_windowed=not args.no_force_windowed,
        keep_retail_palette=not args.force_8bpp_palette,
        gdi_seat=not args.no_gdi_seat,
        agent_present=eff_agent_present,
        force_active=eff_force_active,
        agent_present_every_ticks=eff_present_every,
        show_window_after_ms=eff_show_window_after,
        show_window_client_w=args.show_window_client_w,
        show_window_client_h=args.show_window_client_h,
        turbo=args.turbo,
        turbo_step_ms=args.turbo_step_ms,
        frame_capture=args.frame_capture,
        frame_capture_max=args.frame_capture_max,
        frame_capture_warmup_secs=args.frame_capture_warmup_secs,
        dump_globals=dump_globals,
        dump_regions=dump_regions,
        poke_globals=poke_globals,
        skip_intro=args.skip_intro,
        input_record=args.input_record,
        input_replay_path=(Path(args.input_trace).resolve()
                           if args.input_trace else None),
        input_segtrace_path=(Path(args.input_segtrace).resolve()
                             if args.input_segtrace else None),
        call_trace=args.call_trace,
        call_trace_vas=call_trace_vas,
        call_trace_frames=call_trace_frames,
        call_trace_min_cum_secs=args.call_trace_min_cum_secs,
        call_trace_max_frames=args.call_trace_max_frames,
        anchor_trace=args.anchor_trace,
        capture_draws=args.capture_draws,
        capture_force_redraw=args.capture_force_redraw,
        suppress_edge_scroll=args.suppress_edge_scroll,
        save_sandbox=args.save_sandbox,
        watch=watch_specs,
        state_dump_slots=state_dump_slots,
        rng_seed=rng_seed,
        exe_args=list(args.exe_arg),
    )
    print(f"[capture] run_dir = {run_dir}")
    print(f"[capture] exe     = {cfg.exe}")
    print(f"[capture] cwd     = {cfg.cwd}")
    print(f"[capture] flags   = hide={cfg.hide_window} "
          f"silent={cfg.silent_audio} fwin={cfg.force_windowed} "
          f"gdi_seat={cfg.gdi_seat} "
          f"turbo={cfg.turbo} turbo_step_ms={cfg.turbo_step_ms} "
          f"frame_capture={cfg.frame_capture} "
          f"frame_capture_max={cfg.frame_capture_max} "
          f"frame_capture_warmup_secs={cfg.frame_capture_warmup_secs} "
          f"skip_intro={cfg.skip_intro} "
          f"input_record={cfg.input_record} "
          f"input_replay={cfg.input_replay_path}")

    result = _run_capture_impl(cfg, run_dir)

    meta = {
        "run_id":          run_id,
        "exe":             str(cfg.exe),
        "duration_ms":     result.elapsed_ms,
        "exit_code":       result.exit_code,
        "hwnd_count":      result.hwnd_count,
        "hide_count":      result.hide_count,
        "msgbox_count":    result.msgbox_count,
        "cooplevel_count": result.cooplevel_count,
        "audio_mute_count": result.audio_mute_count,
        "fps_sample_count": result.fps_sample_count,
        "last_current_fps": result.last_current_fps,
        "last_avg_fps":     result.last_avg_fps,
        "last_cum_seconds": result.last_cum_seconds,
        "turbo":            cfg.turbo,
        "turbo_step_ms":    cfg.turbo_step_ms,
        "gdi_seat":         cfg.gdi_seat,
        "gdi_seat_present_count": result.gdi_seat_present_count,
        "frame_capture":             cfg.frame_capture,
        "frame_capture_max":         cfg.frame_capture_max,
        "frame_capture_warmup_secs": cfg.frame_capture_warmup_secs,
        "skip_intro":                cfg.skip_intro,
        "frame_count":               result.frame_count,
        "input_record":              cfg.input_record,
        "input_replay_path":         (str(cfg.input_replay_path)
                                      if cfg.input_replay_path else None),
        "input_event_count":         result.input_event_count,
        "input_replay_post_count":   result.input_replay_post_count,
        "call_trace":                cfg.call_trace,
        "call_trace_vas":            len(cfg.call_trace_vas),
        "call_trace_calls":          result.call_trace_calls,
    }
    (run_dir / "run.json").write_text(json.dumps(meta, indent=2))
    print(json.dumps(meta, indent=2))
    return result.exit_code


if __name__ == "__main__":
    sys.exit(main())
