"""drive/port.py — drive the PORT through the compiled trace.

Wraps tools/run-openlords2.sh (build + fresh-image copy + supervised launch).
`--input-segtrace` is the full seat (title-live bring-up + 640×480 client pin,
engine-quirks #120); the port writes <label>.bmp + anchors.jsonl +
captures.jsonl into --capture-to, and self-terminates when the trace is
exhausted (input_trace_done → g_shutdown).  --frames + the launcher timeout
are backstops for a parked {wait}.
"""
from __future__ import annotations

import os
import subprocess
from pathlib import Path

from ..paths import ROOT


def capture_port(*, compiled: Path, port_dir: Path, max_frames: int,
                 timeout_ms: int, result: dict,
                 rng_seed: bool = True) -> None:
    frames_raw = port_dir / "frames-raw"
    frames_raw.mkdir(parents=True, exist_ok=True)
    log_path = port_dir / "log.txt"
    cmd = [str(ROOT / "tools" / "run-openlords2.sh"), "--",
           "--input-segtrace", str(compiled),
           "--capture-to", str(frames_raw),
           "--frames", str(max_frames)]
    if rng_seed:
        cmd.append("--rng-seed")      # pre-ingame LFSR pin (mirrors retail)
    env = dict(os.environ)
    env["OPENLORDS2_TIMEOUT_MS"] = str(timeout_ms)
    try:
        with log_path.open("w") as log:
            proc = subprocess.run(
                cmd, cwd=ROOT, env=env, stdout=log,
                stderr=subprocess.STDOUT,
                timeout=timeout_ms / 1000 + 120)   # launcher owns the real cap
        result["port_rc"] = proc.returncode
    except subprocess.TimeoutExpired:
        result["port_rc"] = -1
        result["port_error"] = "port run exceeded the harness timeout"
    except Exception as e:                          # noqa: BLE001
        result["port_rc"] = -1
        result["port_error"] = repr(e)
