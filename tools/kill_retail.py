#!/usr/bin/env python3
"""
tools/kill_retail.py — reap stray retail Lords2.exe processes.

Safety net for the capture harness.  Retail is spawned by the (UAC-elevated)
frida-server, so the spawned process runs ELEVATED — non-admin WSL `kill` /
`taskkill.exe` can't touch it.  The elevated frida-server CAN, so this reaps
through the same frida device the capture harness uses.

Use it:
  - manually, when a capture leaked (`tools/kill_retail.py`);
  - as a pre-run cleanup (frida_capture.py calls _reap_by_name at start);
  - in CI teardown.

By default kills every process whose name matches Lords2.exe (case-insensitive).
Pass --name to override, or --pid N (repeatable) to target specific pids.

Stdout is one JSON line (classifier-clean): {"killed":[...], "failed":[...]}.

    nix develop -c python3 tools/kill_retail.py
    nix develop -c python3 tools/kill_retail.py --remote $OPENLORDS2_FRIDA_REMOTE
    nix develop -c python3 tools/kill_retail.py --pid 23048 --pid 24464
"""

from __future__ import annotations

import argparse
import json
import sys

import frida

# Keep in sync with frida_capture.DEFAULT_REMOTE — but importing it drags the
# whole module + frida spawn machinery, so just duplicate the one constant.
DEFAULT_REMOTE = "127.0.0.1:27042"
DEFAULT_NAME   = "Lords2.exe"


def reap(remote: str, name: str | None, pids: list[int]) -> dict:
    dev = frida.get_device_manager().add_remote_device(remote)
    targets: list[int] = list(pids)
    if name:
        nl = name.lower()
        for p in dev.enumerate_processes():
            if p.name.lower() == nl and p.pid not in targets:
                targets.append(p.pid)
    killed, failed = [], []
    for pid in targets:
        try:
            dev.kill(pid)
            killed.append(pid)
        except Exception as e:  # noqa: BLE001 — report, don't raise
            failed.append({"pid": pid, "error": str(e)})
    return {"remote": remote, "killed": killed, "failed": failed}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--remote", default=DEFAULT_REMOTE,
                    help="frida-server host:port (default %(default)s)")
    ap.add_argument("--name", default=DEFAULT_NAME,
                    help="process name to reap (default %(default)s); "
                         "empty string disables name matching")
    ap.add_argument("--pid", type=int, action="append", default=[],
                    help="specific pid to kill (repeatable)")
    args = ap.parse_args(argv)

    try:
        result = reap(args.remote, args.name or None, args.pid)
    except Exception as e:  # noqa: BLE001
        print(json.dumps({"error": str(e), "remote": args.remote}))
        return 1
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
