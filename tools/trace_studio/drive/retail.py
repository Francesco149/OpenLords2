"""drive/retail.py — drive RETAIL through the SAME compiled trace.

Wraps tools/frida_capture.py (remote frida-server on the Windows host):
turbo + skip-intro + hidden window + --rng-seed (canonical LFSR pin — the
port's defaults ARE those seeds, so this aligns retail to the port) +
--anchor-trace.  The run ends early on segtrace_done.  Artifacts are
harvested from runs/capture/<run-id>/ into the session's retail/ dir.
"""
from __future__ import annotations

import datetime as dt
import shutil
import subprocess
import sys
from pathlib import Path

from ..paths import ROOT


def capture_retail(*, compiled: Path, retail_dir: Path, remote: str,
                   rng_seed: bool, result: dict,
                   watch: list[str] | None = None) -> None:
    retail_dir.mkdir(parents=True, exist_ok=True)
    run_id = f"ts-{dt.datetime.now():%Y%m%d-%H%M%S}"
    cmd = [sys.executable, str(ROOT / "tools" / "frida_capture.py"),
           "--remote", remote,
           "--skip-intro", "--turbo",
           "--input-segtrace", str(compiled),
           "--anchor-trace",
           "--run-id", run_id]
    if rng_seed:
        cmd.append("--rng-seed")
    for w in watch or []:
        cmd += ["--watch", w]
    log_path = retail_dir / "log.txt"
    try:
        with log_path.open("w") as log:
            proc = subprocess.run(cmd, cwd=ROOT, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=420)
        result["retail_rc"] = proc.returncode
    except subprocess.TimeoutExpired:
        result["retail_rc"] = -1
        result["retail_error"] = "retail run exceeded the harness timeout"
        return
    except Exception as e:                          # noqa: BLE001
        result["retail_rc"] = -1
        result["retail_error"] = repr(e)
        return

    run_dir = ROOT / "runs" / "capture" / run_id
    if not run_dir.exists():
        result["retail_error"] = f"run dir {run_dir} missing"
        return
    # Harvest: frames-raw (BMPs) + the JSONL side files + the harness log.
    frames_raw = retail_dir / "frames-raw"
    if frames_raw.exists():
        shutil.rmtree(frames_raw)
    src_frames = run_dir / "frames"
    if src_frames.exists():
        shutil.copytree(src_frames, frames_raw)
    for name in ("anchors.jsonl", "captures.jsonl", "watch.jsonl"):
        if (run_dir / name).exists():
            shutil.copy2(run_dir / name, retail_dir / name)
    for extra in run_dir.glob("*.log"):
        shutil.copy2(extra, retail_dir / extra.name)
    result["retail_run_dir"] = str(run_dir)
