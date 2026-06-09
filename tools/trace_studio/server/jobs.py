"""server/jobs.py — the recapture job runner.

One job at a time (a capture owns the retail host + the port build dir).
A job is `python3 tools/trace_studio.py capture --session S [--only X]`
run as a subprocess; stdout/err stream to <session>/job.log so the tray
can tail it.
"""
from __future__ import annotations

import subprocess
import sys
import threading
import time
from pathlib import Path

from ..paths import ROOT


class JobRunner:
    def __init__(self, sess_root: Path):
        self.sess_root = sess_root
        self._lock = threading.Lock()
        self._proc: subprocess.Popen | None = None
        self._job: dict | None = None
        self._seq = 0

    def start(self, session: str, only: str | None,
              remote: str | None = None) -> dict:
        with self._lock:
            if self._proc and self._proc.poll() is None:
                return {"error": "a capture is already running",
                        "job": self._job}
            self._seq += 1
            jid = f"job-{self._seq}"
            cmd = [sys.executable, str(ROOT / "tools" / "trace_studio.py"),
                   "capture", "--session", session]
            if only:
                cmd += ["--only", only]
            if remote:
                cmd += ["--remote", remote]
            log_path = self.sess_root / session / "job.log"
            log_path.parent.mkdir(parents=True, exist_ok=True)
            log = log_path.open("w")
            self._proc = subprocess.Popen(cmd, cwd=ROOT, stdout=log,
                                          stderr=subprocess.STDOUT)
            self._job = {"id": jid, "kind": "capture", "session": session,
                         "only": only, "started": time.time(), "rc": None}
            return {"id": jid}

    def status(self) -> dict:
        with self._lock:
            job = dict(self._job) if self._job else None
            if job and self._proc is not None:
                rc = self._proc.poll()
                job["running"] = rc is None
                job["rc"] = rc
                log_path = self.sess_root / job["session"] / "job.log"
                if log_path.exists():
                    tail = log_path.read_text().splitlines()[-8:]
                    job["log_tail"] = tail
            return {"running": bool(job and job.get("running")),
                    "jobs": [job] if job else []}

    def force_cleanup(self) -> None:
        with self._lock:
            if self._proc and self._proc.poll() is None:
                self._proc.kill()
