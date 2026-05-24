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

DEFAULT_REMOTE = os.environ.get(
    "OPENLORDS2_FRIDA_REMOTE",
    os.environ.get("OPENMARE_FRIDA_REMOTE",
                   os.environ.get("OPENRECET_FRIDA_REMOTE", "127.0.0.1:27042")))

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
    turbo:             bool = False
    turbo_step_ms:     int  = 17        # virtual-ms per per-tick driver iteration
    frame_capture:              bool = False
    frame_capture_max:          int  = 16   # 0 = unlimited
    frame_capture_warmup_secs:  int  = 5    # skip captures for first N cum-secs
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


def _run_capture_impl(cfg: CaptureConfig, run_dir: Path) -> CaptureResult:
    agent_log = run_dir / "agent.log"
    f_log = agent_log.open("w", buffering=1)

    counts = {"hwnd": 0, "hide": 0, "msgbox": 0, "ddraw_coop": 0,
              "dsound_coop": 0, "audio_mute": 0, "display_blocked": 0,
              "fps_sample": 0, "frame": 0}
    fps_last = {"current_fps": 0, "avg_fps": 0, "cum_secs": 0}
    done = threading.Event()

    frames_dir = run_dir / "frames"
    if cfg.frame_capture:
        frames_dir.mkdir(exist_ok=True)

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
                counts["frame"] += 1
                f_log.write(f"[frame] #{fi} {w}x{h} -> {out.name} "
                            f"virt_ms={p.get('virtual_now_ms')} "
                            f"t_ms={p.get('t_ms')}\n")
            except Exception as e:
                f_log.write(f"[frame] write failed {out}: {e}\n")
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

    session = None
    script  = None
    pid     = None
    exit_code = 0

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
            "turbo":                     bool(cfg.turbo),
            "turbo_step_ms":             int(cfg.turbo_step_ms),
            "frame_capture":             bool(cfg.frame_capture),
            "frame_capture_max":         int(cfg.frame_capture_max),
            "frame_capture_warmup_secs": int(cfg.frame_capture_warmup_secs),
        }
        script.exports_sync.init(init_cfg)
        device.resume(pid)

        deadline = t0 + (cfg.duration_ms / 1000.0)
        while not done.is_set() and time.monotonic() < deadline:
            time.sleep(0.05)

        elapsed_ms = int((time.monotonic() - t0) * 1000)
    finally:
        if script is not None:
            try: script.unload()
            except Exception as e: f_log.write(f"[shutdown] script unload: {e}\n")
        if session is not None:
            try: session.detach()
            except Exception as e: f_log.write(f"[shutdown] session detach: {e}\n")
        if pid is not None:
            try: device.kill(pid)
            except Exception as e:
                f_log.write(f"[shutdown] kill pid={pid}: {e}\n")
                exit_code = 1
        signal.signal(signal.SIGTERM, prev_term)
        signal.signal(signal.SIGINT,  prev_int)

    elapsed_ms = int((time.monotonic() - t0) * 1000)
    f_log.write(f"[summary] {counts}\n")
    f_log.close()

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
    ap.add_argument("--exe-arg", action="append", default=[],
                    help="argument to pass to the spawned exe (repeatable)")
    args = ap.parse_args(argv)

    run_id = args.run_id or dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = ROOT / "runs" / "capture" / run_id
    run_dir.mkdir(parents=True, exist_ok=True)

    cfg = CaptureConfig(
        duration_ms=args.duration_ms,
        remote=args.remote,
        exe=Path(args.exe).resolve(),
        cwd=Path(args.cwd).resolve(),
        hide_window=not args.no_hide,
        silent_audio=not args.no_silent_audio,
        force_windowed=not args.no_force_windowed,
        turbo=args.turbo,
        turbo_step_ms=args.turbo_step_ms,
        frame_capture=args.frame_capture,
        frame_capture_max=args.frame_capture_max,
        frame_capture_warmup_secs=args.frame_capture_warmup_secs,
        exe_args=list(args.exe_arg),
    )
    print(f"[capture] run_dir = {run_dir}")
    print(f"[capture] exe     = {cfg.exe}")
    print(f"[capture] cwd     = {cfg.cwd}")
    print(f"[capture] flags   = hide={cfg.hide_window} "
          f"silent={cfg.silent_audio} fwin={cfg.force_windowed} "
          f"turbo={cfg.turbo} turbo_step_ms={cfg.turbo_step_ms} "
          f"frame_capture={cfg.frame_capture} "
          f"frame_capture_max={cfg.frame_capture_max} "
          f"frame_capture_warmup_secs={cfg.frame_capture_warmup_secs}")

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
        "frame_capture":             cfg.frame_capture,
        "frame_capture_max":         cfg.frame_capture_max,
        "frame_capture_warmup_secs": cfg.frame_capture_warmup_secs,
        "frame_count":               result.frame_count,
    }
    (run_dir / "run.json").write_text(json.dumps(meta, indent=2))
    print(json.dumps(meta, indent=2))
    return result.exit_code


if __name__ == "__main__":
    sys.exit(main())
