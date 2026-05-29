#!/usr/bin/env python3
"""
tools/reaper.py — detached liveness reaper for the capture harness.

Closes the one leak path the controller can't close itself: the controller
(frida_capture.py) being SIGKILLed / OOM-killed / crashing.  When that happens
there is no `finally`, the controller-side watchdog thread dies with it, and
frida tears down the in-target agent the instant the session drops — so the
(elevated, non-admin-unkillable) retail process would leak.

frida_capture spawns this reaper DETACHED (its own session, not in the
controller's process group) and hands it:
  - the spawned retail pid,
  - a heartbeat file the controller `touch`es every loop iteration,
  - a staleness threshold and a max lifetime.

The reaper polls:
  - heartbeat file GONE        → controller did a clean teardown → exit, no kill.
  - heartbeat file STALE       → controller died → kill retail (via the elevated
                                 frida device) + reap by name → exit.
  - max lifetime exceeded      → safety: reap + exit.

Because it's an ordinary non-elevated WSL python process it's itself trivially
killable, and it always terminates (clean signal, staleness, or max-life), so
the reaper can't become a leak of its own.

Stdout/stderr go to --log; the process is otherwise silent.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import frida


def _log(fp, msg: str) -> None:
    try:
        fp.write(f"[{time.strftime('%H:%M:%S')}] {msg}\n")
        fp.flush()
    except Exception:  # noqa: BLE001
        pass


def _reap(remote: str, pid: int, name: str, fp) -> None:
    try:
        dev = frida.get_device_manager().add_remote_device(remote)
    except Exception as e:  # noqa: BLE001
        _log(fp, f"device connect failed: {e}")
        return
    # Direct pid kill first, then sweep by name in case it respawned / the pid
    # is stale.
    try:
        dev.kill(pid)
        _log(fp, f"killed spawned pid={pid}")
    except Exception as e:  # noqa: BLE001
        _log(fp, f"kill pid={pid} failed ({e}); sweeping by name")
    try:
        nl = name.lower()
        for p in dev.enumerate_processes():
            if p.name.lower() == nl:
                try:
                    dev.kill(p.pid)
                    _log(fp, f"swept {name} pid={p.pid}")
                except Exception as e:  # noqa: BLE001
                    _log(fp, f"sweep kill {p.pid} failed: {e}")
    except Exception as e:  # noqa: BLE001
        _log(fp, f"enumerate failed: {e}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--remote", required=True)
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--name", default="Lords2.exe")
    ap.add_argument("--heartbeat-file", type=Path, required=True)
    ap.add_argument("--log", type=Path, required=True)
    ap.add_argument("--stale-s", type=float, default=15.0,
                    help="reap if the heartbeat file goes unmodified this long")
    ap.add_argument("--max-life-s", type=float, default=600.0,
                    help="hard cap on reaper lifetime; reap + exit when hit")
    args = ap.parse_args(argv)

    fp = args.log.open("a", buffering=1)
    _log(fp, f"reaper up: pid={args.pid} name={args.name} "
             f"hb={args.heartbeat_file} stale={args.stale_s}s "
             f"max_life={args.max_life_s}s")
    t0 = time.monotonic()

    while True:
        time.sleep(1.0)
        now = time.monotonic()

        if not args.heartbeat_file.exists():
            _log(fp, "heartbeat file gone — controller teardown clean, exiting")
            return 0

        try:
            age = time.time() - args.heartbeat_file.stat().st_mtime
        except OSError:
            _log(fp, "heartbeat stat failed — treating as clean exit")
            return 0

        if age > args.stale_s:
            _log(fp, f"heartbeat stale ({age:.1f}s > {args.stale_s}s) — "
                     f"controller died; reaping")
            _reap(args.remote, args.pid, args.name, fp)
            return 0

        if (now - t0) > args.max_life_s:
            _log(fp, f"max lifetime ({args.max_life_s}s) exceeded — reaping + exit")
            _reap(args.remote, args.pid, args.name, fp)
            return 0


if __name__ == "__main__":
    sys.exit(main())
