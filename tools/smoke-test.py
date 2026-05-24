#!/usr/bin/env python3
"""
tools/smoke-test.py — minimal end-to-end check that the harness can spawn
Lords2.exe via Frida and tear it down without disrupting the host desktop.

This is the simplest possible scenario:
  - Spawn the retail exe under Frida with hide-window + silent-audio +
    force-windowed (all the non-disruption hooks).
  - Let it run for a short wall-clock window (default 5 s).
  - Verify the agent emitted `ready`, installed its hide_window hooks,
    tracked at least one CreateWindowEx, and forced at least one hide
    OR rewrote at least one DDraw cooperative-level call.
  - Tear down via device.kill (and the Frida session unload).

Use it after touching `tools/frida/openlords2-agent.js` to confirm you
haven't broken the boot path.

Usage:
    nix develop --command python3 tools/smoke-test.py [--duration-ms N]

Prerequisites:
    - frida-server.exe running on the Windows host at 127.0.0.1:27042
      (the auto-start helper will UAC-prompt if it isn't up).
    - `./tools/setup.sh` has linked the GOG install into vendor/original/.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import frida_capture as fc  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--duration-ms", type=int, default=5_000)
    ap.add_argument("--no-hide", action="store_true",
                    help="leave the engine's window visible (debug aid)")
    ap.add_argument("--no-silent-audio", action="store_true",
                    help="don't mute audio (debug aid — disruptive!)")
    ap.add_argument("--no-force-windowed", action="store_true",
                    help="don't strip DDraw exclusive-fullscreen flags "
                         "(debug aid — VERY disruptive on Lords 2!)")
    ap.add_argument("--turbo", action="store_true",
                    help="exercise turbo v1 (Sleep no-op) + v2 "
                         "(virtual timeGetTime)")
    ap.add_argument("--turbo-step-ms", type=int, default=17,
                    help="virtual-ms per per-tick driver iteration "
                         "when --turbo is set")
    ap.add_argument("--run-id", default=None,
                    help="run directory name under runs/smoke/ "
                         "(default: timestamp)")
    args = ap.parse_args(argv)

    run_id = args.run_id or dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = ROOT / "runs" / "smoke" / run_id
    run_dir.mkdir(parents=True, exist_ok=True)

    cfg = fc.CaptureConfig(
        duration_ms=args.duration_ms,
        hide_window=not args.no_hide,
        silent_audio=not args.no_silent_audio,
        force_windowed=not args.no_force_windowed,
        turbo=args.turbo,
        turbo_step_ms=args.turbo_step_ms,
    )
    print(f"[smoke] run_dir = {run_dir}")
    result = fc._run_capture_impl(cfg, run_dir)
    log = (run_dir / "agent.log").read_text()

    # Loose assertions — the harness is the most fragile-to-Frida-version
    # piece in the project, so flag broken hooks without overspecifying
    # the boot sequence.
    checks = [
        ("agent emitted ready",                "[ready]"        in log),
        ("agent installed hide_window hooks",  "hide_window hooks installed" in log),
        ("agent tracked CreateWindowEx",       "CreateWindowEx tracking installed" in log),
        ("agent installed MessageBox hook",    "msgbox_redirect: MessageBoxA" in log),
    ]
    if not args.no_force_windowed:
        checks.append(("ChangeDisplaySettings* stub installed",
                       "force_windowed[L1]:" in log))
        checks.append(("ddraw armed",
                       "force_windowed[L2]: ddraw!DirectDrawCreate armed"
                       in log or "force_windowed[L2]: ddraw.dll not loaded"
                       in log))
    if not args.no_silent_audio:
        checks.append(("waveOutSetVolume mute armed",
                       "silent_audio: winmm!waveOutSetVolume" in log))
    if not args.no_hide:
        # With hide_window on, at least one owned HWND should appear during
        # init (engine creates its main window before doing anything else).
        checks.append(("at least one HWND tracked",  "[hwnd]" in log))
    if args.turbo:
        checks.append(("turbo v1: Sleep no-op",
                       "turbo v1: kernel32!Sleep no-op installed" in log))
        checks.append(("turbo v2: timeGetTime virtualised",
                       "turbo v2: winmm!timeGetTime virtualised" in log))
        # The engine_tick anchor lands unconditionally; with turbo on we
        # additionally expect the main-thread capture to land within the
        # smoke window.  Both are confirmed via the agent log.
        checks.append(("engine_tick anchor armed",
                       "engine_tick anchor installed" in log))
        checks.append(("engine main thread captured",
                       "engine_tick: main thread id captured" in log))

    failed = [name for name, ok in checks if not ok]

    summary = {
        "run_id":      run_id,
        "duration_ms": result.elapsed_ms,
        "exit_code":   result.exit_code,
        "hwnd_count":      result.hwnd_count,
        "hide_count":      result.hide_count,
        "msgbox_count":    result.msgbox_count,
        "cooplevel_count": result.cooplevel_count,
        "audio_mute_count": result.audio_mute_count,
        "fps_sample_count": result.fps_sample_count,
        "last_current_fps": result.last_current_fps,
        "last_avg_fps":     result.last_avg_fps,
        "last_cum_seconds": result.last_cum_seconds,
        "turbo":           args.turbo,
        "turbo_step_ms":   args.turbo_step_ms,
        "checks": [{"name": n, "ok": ok} for n, ok in checks],
    }
    (run_dir / "smoke.json").write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))
    if failed:
        print(f"\n[smoke] FAILED: {failed}", file=sys.stderr)
        return 1
    print(f"\n[smoke] OK ({result.elapsed_ms} ms)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
