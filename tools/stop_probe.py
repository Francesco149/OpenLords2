#!/usr/bin/env python3
"""tools/stop_probe.py — stop THIS project's probe daemon ONLY.

HARD RULE: never `pkill -f probe_daemon.py` — other projects may run their own probe
daemons in parallel and a broad pattern kill would take them down too. This targets only
our daemon: the pid recorded in runs/probe/daemon.json (written by our probe_daemon),
optionally narrowed to a --run-id. It first asks the daemon to `quit` (clean: also stops
the spawned Lords2.exe), then SIGTERMs that exact pid if it lingers. No pattern matching
against every process named probe_daemon.

    nix develop -c python3 tools/stop_probe.py            # stop the daemon in daemon.json
    nix develop -c python3 tools/stop_probe.py --run-id snow3   # only if it matches
"""
from __future__ import annotations
import argparse, json, os, signal, socket, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CTL = ROOT / "runs" / "probe" / "daemon.json"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run-id", default=None, help="only stop if daemon.json's run_dir ends with this")
    ap.add_argument("--port", type=int, default=None, help="override port (else from daemon.json)")
    a = ap.parse_args()

    if not CTL.exists():
        print("no runs/probe/daemon.json — nothing of ours to stop"); return
    info = json.loads(CTL.read_text())
    pid, port, run_dir = info.get("pid"), a.port or info.get("port"), info.get("run_dir", "")
    if a.run_id and not str(run_dir).rstrip("/").endswith(a.run_id):
        print(f"daemon.json run_dir={run_dir!r} != --run-id {a.run_id!r}; NOT ours, refusing to kill"); return

    # 1) clean quit over the socket (also stops the game); only touches OUR port.
    try:
        s = socket.socket(); s.settimeout(4); s.connect(("127.0.0.1", port))
        s.sendall(b'{"cmd":"quit"}\n'); s.recv(256); s.close()
        print(f"sent quit to :{port} (pid {pid}, {run_dir})")
    except Exception as e:
        print(f"quit rpc failed ({e!r}) — will SIGTERM pid {pid}")

    # 2) SIGTERM the EXACT pid if it's still alive (never a pattern kill).
    if pid:
        for _ in range(10):
            try:
                os.kill(pid, 0)          # exists?
            except ProcessLookupError:
                print(f"pid {pid} gone — stopped cleanly"); return
            except PermissionError:
                print(f"pid {pid} not ours (permission) — leaving it"); return
            time.sleep(0.3)
        try:
            os.kill(pid, signal.SIGTERM); print(f"SIGTERM'd pid {pid}")
        except Exception as e:
            print(f"could not SIGTERM {pid}: {e!r}")


if __name__ == "__main__":
    main()
