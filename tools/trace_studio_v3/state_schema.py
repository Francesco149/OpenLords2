"""state_schema.py — the save-schema slot list, parsed from the C table.

src/save_schema_table.h is the single source of truth (each
`{ symbol, size }, /* idx retail 0xVA */`). The retail agent needs the (va, size)
list to read the same slots the port dumps; the viewer wants the symbol names.
Parsed, not duplicated. (Absorbed into trace_studio_v3 when the web studio was
retired — was tools/trace_studio/model/schema.py.)
"""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent     # repo root

_ROW = re.compile(
    r"^\s*\{\s*&?([A-Za-z0-9_]+)\s*,\s*(\d+)\s*\}\s*,\s*"
    r"/\*\s*(\d+)\s+retail\s+(0x[0-9a-fA-F]+)\s*\*/")


def load_slots(root: Path = ROOT) -> list[dict]:
    """→ [{i, name, va, size}] in slot order (i == list index)."""
    src = (root / "src" / "save_schema_table.h").read_text()
    out: list[dict] = []
    for line in src.splitlines():
        m = _ROW.match(line)
        if not m:
            continue
        out.append({"i": int(m.group(3)), "name": m.group(1),
                    "va": int(m.group(4), 16), "size": int(m.group(2))})
    out.sort(key=lambda r: r["i"])
    if [r["i"] for r in out] != list(range(len(out))):
        raise SystemExit("schema parse: slot indices aren't dense — "
                         "save_schema_table.h layout changed?")
    return out


def write_slots_file(path: Path, root: Path = ROOT) -> int:
    slots = load_slots(root)
    path.write_text(json.dumps(
        [{"i": s["i"], "va": s["va"], "size": s["size"]} for s in slots]))
    return len(slots)
