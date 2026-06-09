"""analysis/pixeldiff.py — per-label port↔retail pixel diff + verdict.

Per pair: n_px (pixels differing in any channel), pct, mean (mean abs over
all samples), max, and an amplified diff PNG (abs(a-b)*amp).  Status per
label: identical | clean (pct ≤ CLEAN_PCT) | diff | missing_port |
missing_retail | size_mismatch.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image

CLEAN_PCT = 0.05          # ≤ 0.05% differing pixels still counts as clean


def diff_pair(a_png: Path, b_png: Path, out_png: Path,
              amp: float = 6.0) -> dict:
    a = np.asarray(Image.open(a_png).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(b_png).convert("RGB"), dtype=np.int16)
    if a.shape != b.shape:
        return {"status": "size_mismatch",
                "port_size": list(a.shape[1::-1]),
                "retail_size": list(b.shape[1::-1])}
    d = np.abs(a - b)
    n_px = int((d.max(axis=2) > 0).sum())
    total = a.shape[0] * a.shape[1]
    pct = 100.0 * n_px / total
    stats = {
        "n_px": n_px,
        "pct": round(pct, 4),
        "mean": round(float(d.mean()), 4),
        "max": int(d.max()),
    }
    stats["status"] = ("identical" if n_px == 0
                       else "clean" if pct <= CLEAN_PCT else "diff")
    if n_px > 0:
        out_png.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(
            np.clip(d * amp, 0, 255).astype(np.uint8)).save(out_png)
        stats["file"] = out_png.name
    return stats


def build_captures(plan: list[dict], port_map: dict[str, dict],
                   retail_map: dict[str, dict], sess_dir: Path,
                   amp: float, want_retail: bool) -> list[dict]:
    """The manifest's captures[] — plan order, label-paired, diffed."""
    captures: list[dict] = []
    for p in plan:
        label = p["label"]
        entry: dict = {"label": label, "group": p["group"], "n": p["n"],
                       "seg": p["seg"]}
        in_port = label in port_map
        in_retail = label in retail_map
        if in_port:
            entry["port"] = port_map[label]
        if in_retail:
            entry["retail"] = retail_map[label]
        if in_port and in_retail:
            entry["diff"] = diff_pair(
                sess_dir / "port" / "frames" / f"{label}.png",
                sess_dir / "retail" / "frames" / f"{label}.png",
                sess_dir / "diff" / f"{label}.png", amp)
        elif want_retail:
            entry["diff"] = {"status": ("missing_retail" if in_port
                                        else "missing_port" if in_retail
                                        else "missing_both")}
        captures.append(entry)
    return captures


def verdict(captures: list[dict], want_retail: bool) -> dict:
    """Roll-up: clean iff every pair is identical/clean and none missing."""
    if not want_retail:
        return {"status": "port-only", "n_diff": None}
    statuses = [(c.get("diff") or {}).get("status") for c in captures]
    n_missing = sum(1 for s in statuses if s and s.startswith("missing")
                    or s == "size_mismatch")
    n_diff = sum(1 for s in statuses if s == "diff")
    worst = None
    worst_px = -1
    for c in captures:
        d = c.get("diff") or {}
        if d.get("status") == "diff" and d.get("n_px", 0) > worst_px:
            worst_px = d["n_px"]
            worst = {"label": c["label"], "n_px": d["n_px"],
                     "pct": d.get("pct")}
    status = ("clean" if n_diff == 0 and n_missing == 0
              else "missing" if n_diff == 0 else "diff")
    return {"status": status, "n_diff": n_diff, "n_missing": n_missing,
            "worst": worst, "clean_pct_threshold": CLEAN_PCT}
