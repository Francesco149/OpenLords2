"""transport/convert.py — raw captures → label-keyed PNGs.

Port side: frames-raw/<label>.bmp (the port names by label already) →
frames/<label>.png; its anchors.jsonl/captures.jsonl move up to port/.
Retail side: frames-raw/frame_<tick:05d>.bmp + captures.jsonl ({label,frame})
→ frames/<label>.png (copy semantics — two labels may share a tick).

Both converters are idempotent: a cached side (frames/ present, frames-raw/
gone) just reloads its label map from captures.jsonl.
"""
from __future__ import annotations

import shutil
from pathlib import Path

from PIL import Image

from ..model.ops import read_jsonl


def _to_png(src_bmp: Path, dst_png: Path) -> bool:
    try:
        Image.open(src_bmp).convert("RGB").save(dst_png)
        return True
    except OSError:
        return False


def convert_port(port_dir: Path) -> dict[str, dict]:
    """→ {label: {"frame": N}} for every converted port capture."""
    raw = port_dir / "frames-raw"
    frames = port_dir / "frames"
    # The port writes the side files into the capture dir; lift them up.
    for name in ("anchors.jsonl", "captures.jsonl"):
        if (raw / name).exists():
            shutil.move(str(raw / name), str(port_dir / name))
    rows = read_jsonl(port_dir / "captures.jsonl")
    out: dict[str, dict] = {}
    frames.mkdir(parents=True, exist_ok=True)
    for r in rows:
        label = str(r.get("label") or "")
        if not label:
            continue
        dst = frames / f"{label}.png"
        if not dst.exists():
            src = raw / f"{label}.bmp"
            if not src.exists() or not _to_png(src, dst):
                continue
        out[label] = {"frame": int(r.get("frame", 0))}
    return out


def convert_retail(retail_dir: Path) -> dict[str, dict]:
    """→ {label: {"frame": N}} for every converted retail capture.

    Retail BMPs are named by a SEQUENTIAL capture index (frame_00000.bmp …),
    not the tick — captures.jsonl carries the label→tick map, and the agent's
    tick-keyed Set fires captures in ascending-tick order, so sorted unique
    ticks pair 1:1 with the file sequence.  (Two labels scheduled on the same
    tick share one BMP.)
    """
    raw = retail_dir / "frames-raw"
    frames = retail_dir / "frames"
    rows = read_jsonl(retail_dir / "captures.jsonl")
    ticks = sorted({int(r.get("frame", 0)) for r in rows
                    if r.get("label")})
    tick_idx = {t: i for i, t in enumerate(ticks)}
    out: dict[str, dict] = {}
    frames.mkdir(parents=True, exist_ok=True)
    for r in rows:
        label = str(r.get("label") or "")
        tick = int(r.get("frame", 0))
        if not label:
            continue
        dst = frames / f"{label}.png"
        if not dst.exists():
            src = raw / f"frame_{tick_idx[tick]:05d}.bmp"
            if not src.exists() or not _to_png(src, dst):
                continue
        out[label] = {"frame": tick}
    return out


def collect_state_port(port_dir: Path) -> dict[str, Path]:
    """Port state snapshots: frames-raw/<label>.state.json → state/<label>.json
    (idempotent; a cached side just lists state/)."""
    raw = port_dir / "frames-raw"
    sdir = port_dir / "state"
    sdir.mkdir(parents=True, exist_ok=True)
    rows = read_jsonl(port_dir / "captures.jsonl")
    out: dict[str, Path] = {}
    for r in rows:
        label = str(r.get("label") or "")
        if not label:
            continue
        dst = sdir / f"{label}.json"
        if not dst.exists():
            src = raw / f"{label}.state.json"
            if src.exists():
                shutil.copy2(src, dst)
        if dst.exists():
            out[label] = dst
    return out


def collect_state_retail(retail_dir: Path) -> dict[str, Path]:
    """Retail state snapshots: state-raw/state_<tick>.json (named by the TICK
    the agent stamped) → state/<label>.json via captures.jsonl."""
    raw = retail_dir / "state-raw"
    sdir = retail_dir / "state"
    sdir.mkdir(parents=True, exist_ok=True)
    rows = read_jsonl(retail_dir / "captures.jsonl")
    out: dict[str, Path] = {}
    for r in rows:
        label = str(r.get("label") or "")
        tick = int(r.get("frame", 0))
        if not label:
            continue
        dst = sdir / f"{label}.json"
        if not dst.exists():
            src = raw / f"state_{tick:05d}.json"
            if src.exists():
                shutil.copy2(src, dst)
        if dst.exists():
            out[label] = dst
    return out


def prune_raw(side_dir: Path) -> None:
    for sub in ("frames-raw", "state-raw"):
        raw = side_dir / sub
        if raw.exists():
            shutil.rmtree(raw)
