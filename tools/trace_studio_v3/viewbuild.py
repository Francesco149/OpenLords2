#!/usr/bin/env python3
"""Trace Studio v3 — view.json builder (the identity join the viewer consumes).

A view.json names the two .olv3 containers and lists COLUMNS: each column pairs a
port kept-frame index with a retail kept-frame index under a shared identity label
(an anchor + offset, or a trace label), optionally carrying the save-schema STATE
snapshot for that column. The viewer reconstructs frames live from the containers
and shows them port|retail|diff per column — nothing is baked.

Because viewer.exe is a Windows process (run via WSLInterop), the container +
notes paths inside view.json must be WINDOWS paths; win_path() converts. This
module is imported by the capture orchestrator and is runnable standalone to emit a
synthetic two-container demo for self-verifying the viewer.
"""
from __future__ import annotations

import json
import subprocess
from pathlib import Path


def win_path(p: str | Path) -> str:
    """WSL path → Windows path (UNC \\\\wsl.localhost\\... for /tmp etc.). The
    viewer (a Windows process) opens these; reading a UNC is fine."""
    return subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True,
                          check=True).stdout.strip()


def write_view(out_path: str | Path, *, port_container: str | Path,
               retail_container: str | Path, columns: list[dict],
               scenario: str = "", anchor: str = "", verdict: str = "",
               note: str = "", notes_path: str | Path | None = None,
               win_paths: bool = True) -> Path:
    """Emit view.json. `columns` items: {label, offset, port_idx, retail_idx,
    port_present, retail_present, gap, state:{port,retail}}. Paths are converted to
    Windows form unless win_paths=False (host-side tests reading via the C CLI)."""
    conv = win_path if win_paths else (lambda x: str(x))
    has_state = any("state" in c for c in columns)
    view = {
        "scenario": scenario,
        "anchor": anchor,
        "verdict": verdict,
        "note": note,
        "has_state": has_state,
        "port_container": conv(port_container),
        "retail_container": conv(retail_container),
        "frames": columns,
    }
    if notes_path is not None:
        view["notes_path"] = conv(notes_path)
    out_path = Path(out_path)
    out_path.write_text(json.dumps(view, indent=1))
    return out_path


def synth_demo(out_dir: str | Path) -> Path:
    """Build a self-contained demo: port.olv3 (box shift 0) + retail.olv3 (shift 2,
    so the diff lights up) + view.json (1:1 columns, two carrying injected state, one
    gap). Returns the view.json path. For headless --shot self-verification."""
    import olv3
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    n = 24
    olv3.make_synthetic(out_dir / "port.olv3", n=n, shift=0, pal_perturb=0)
    olv3.make_synthetic(out_dir / "retail.olv3", n=n, shift=2, pal_perturb=40)
    cols: list[dict] = []
    for i in range(n):
        col = {
            "label": f"DEMO+{i}", "offset": i,
            "port_idx": i, "retail_idx": i,
            "port_present": 100 + i, "retail_present": 100 + i,
        }
        if i in (3, 10):   # exercise the state panel + a real divergence
            col["state"] = {
                "retail": {"rng_calls": 1000 + i, "screen": 31, "cx": 0.6},
                "port":   {"rng_calls": 1000 + i + (1 if i == 10 else 0), "screen": 31, "cx": 0.6},
            }
        cols.append(col)
    # a gap column (retail missing) to exercise the placeholder + ribbon
    cols.insert(12, {"label": "DEMO+gap", "offset": 12, "port_idx": 5, "gap": "retail",
                     "port_present": 200})
    return write_view(out_dir / "view.json", port_container=out_dir / "port.olv3",
                      retail_container=out_dir / "retail.olv3", columns=cols,
                      scenario="synthetic-demo", anchor="DEMO",
                      verdict="DIVERGENT (box +2px)", notes_path=out_dir / "notes.json")


if __name__ == "__main__":
    import sys
    d = sys.argv[1] if len(sys.argv) > 1 else "/tmp/olv3demo"
    vp = synth_demo(d)
    print(f"view.json -> {vp}")
    print(f"win path  -> {win_path(vp)}")
