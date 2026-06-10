"""analysis/statediff.py — per-label save-schema state diff.

Both sides dump {"slots":[v|"h:xxxxxxxx"|null,…]} in schema order at each
capture instant (port: src/state_dump.c; retail: the agent's stateDumpOnce).
Diff = slot-wise compare, annotated with the schema symbol names.
"""
from __future__ import annotations

import json
from pathlib import Path

MAX_DIFFS_LISTED = 64


def _load(path: Path) -> dict | None:
    if not path.exists():
        return None
    try:
        d = json.loads(path.read_text())
        return d if isinstance(d, dict) else None
    except ValueError:
        return None


def diff_label(port_json: Path, retail_json: Path,
               names: list[str]) -> dict | None:
    """→ {"n_diff", "diffs":[{i,name,port,retail[,sub_diff]}]} or None when a
    side is missing (state parity simply not available for this label).

    sub_diff (present when both sides dumped per-block sub-hashes): the
    16-range indices whose sub-hash differs — localizes a block divergence
    to byte ranges [r*size/16, (r+1)*size/16)."""
    da = _load(port_json)
    db = _load(retail_json)
    if da is None or db is None:
        return None
    a, b = da.get("slots"), db.get("slots")
    if a is None or b is None:
        return None
    sub_a, sub_b = da.get("sub") or {}, db.get("sub") or {}
    n = min(len(a), len(b))
    diffs = []
    for i in range(n):
        if a[i] != b[i]:
            entry = {"i": i,
                     "name": names[i] if i < len(names) else f"slot{i}",
                     "port": a[i], "retail": b[i]}
            sa, sb = sub_a.get(str(i)), sub_b.get(str(i))
            if sa and sb:
                entry["sub_diff"] = [r for r in range(min(len(sa), len(sb)))
                                     if sa[r] != sb[r]]
            diffs.append(entry)
    out = {"n_diff": len(diffs), "n_slots": n,
           "diffs": diffs[:MAX_DIFFS_LISTED]}
    if len(a) != len(b):
        out["len_mismatch"] = [len(a), len(b)]
    return out
