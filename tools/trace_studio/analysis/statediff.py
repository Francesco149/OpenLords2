"""analysis/statediff.py — per-label save-schema state diff.

Both sides dump {"slots":[v|"h:xxxxxxxx"|null,…]} in schema order at each
capture instant (port: src/state_dump.c; retail: the agent's stateDumpOnce).
Diff = slot-wise compare, annotated with the schema symbol names.
"""
from __future__ import annotations

import json
from pathlib import Path

MAX_DIFFS_LISTED = 64


def _load(path: Path) -> list | None:
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text()).get("slots")
    except ValueError:
        return None


def diff_label(port_json: Path, retail_json: Path,
               names: list[str]) -> dict | None:
    """→ {"n_diff", "diffs":[{i,name,port,retail}]} or None when a side is
    missing (state parity simply not available for this label)."""
    a = _load(port_json)
    b = _load(retail_json)
    if a is None or b is None:
        return None
    n = min(len(a), len(b))
    diffs = []
    for i in range(n):
        if a[i] != b[i]:
            diffs.append({"i": i,
                          "name": names[i] if i < len(names) else f"slot{i}",
                          "port": a[i], "retail": b[i]})
    out = {"n_diff": len(diffs), "n_slots": n,
           "diffs": diffs[:MAX_DIFFS_LISTED]}
    if len(a) != len(b):
        out["len_mismatch"] = [len(a), len(b)]
    return out
