"""drive/runner.py — concurrent port+retail drive.

The two sides are fully independent (each replays the same compiled trace
file read-only), so they just run in parallel threads.  The port is local
(seconds); retail is the remote frida host (tens of seconds).
"""
from __future__ import annotations

import threading
from pathlib import Path

from . import port as port_drive
from . import retail as retail_drive


def drive(*, compiled: Path, port_dir: Path, retail_dir: Path,
          run_port: bool, run_retail: bool, remote: str, rng_seed: bool,
          port_max_frames: int, port_timeout_ms: int,
          watch: list[str] | None = None,
          state_slots: Path | None = None) -> dict:
    result: dict = {}
    threads: list[threading.Thread] = []
    if run_port:
        t = threading.Thread(target=port_drive.capture_port, kwargs=dict(
            compiled=compiled, port_dir=port_dir,
            max_frames=port_max_frames, timeout_ms=port_timeout_ms,
            rng_seed=rng_seed, result=result))
        t.start()
        threads.append(t)
    if run_retail:
        t = threading.Thread(target=retail_drive.capture_retail, kwargs=dict(
            compiled=compiled, retail_dir=retail_dir, remote=remote,
            rng_seed=rng_seed, watch=watch, state_slots=state_slots,
            result=result))
        t.start()
        threads.append(t)
    for t in threads:
        t.join()
    return result
