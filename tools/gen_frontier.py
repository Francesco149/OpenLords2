#!/usr/bin/env python3
"""Derive the *port frontier*: the unported engine functions that are already
reachable from ported code — i.e. the natural next chips.

Two artifacts already in the tree make this a pure derivation (no drift):

  - docs/decompiled/by-address/<va>.c — per-function Ghidra decompile; the
    `FUN_<va>` tokens in a body are that function's call edges.
  - docs/port-ledger.json — which VAs are already touched (run
    gen_port_ledger.py first; this script reads its output).

A function is on the **frontier** when it is unported + non-thunk and at least
one *touched* (ported/tested) function calls it. It is a **leaf** of the
frontier when every function IT calls is already ported — so it can be ported
today with zero new dependencies. Leaves, sorted by how many ported callers
want them, are the recommended porting order.

Output (git-tracked, no vendor bytes):
  docs/port-frontier.md   ranked frontier table + leaf shortlist, by address band

Run:  python3 tools/gen_port_ledger.py && python3 tools/gen_frontier.py
"""

from __future__ import annotations

import csv
import json
import re
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FUNCTIONS_CSV = REPO / "docs" / "decompiled" / "functions.csv"
BY_ADDR = REPO / "docs" / "decompiled" / "by-address"
LEDGER_JSON = REPO / "docs" / "port-ledger.json"
OUT_MD = REPO / "docs" / "port-frontier.md"

FUN_RE = re.compile(r"FUN_([0-9a-fA-F]{6,8})")

# Coarse address-band → subsystem hints, derived from where ported clusters
# already live (see findings/INDEX.md). Bands are approximate; they exist only
# to group the table, not to assert ground truth.
BANDS = [
    (0x401000, 0x410000, "early/runtime + low-level helpers"),
    (0x410000, 0x420000, "render-quad / blit primitives"),
    (0x420000, 0x430000, "strategic-map camera + scroll"),
    (0x430000, 0x440000, "stage/post-load + char tables"),
    (0x440000, 0x450000, "sim dispatch + records"),
    (0x450000, 0x460000, "fade / loading / audio-mci"),
    (0x460000, 0x470000, "tables (item/char) + populators"),
    (0x470000, 0x480000, "d3d/mesh pool + preload + palette"),
    (0x480000, 0x490000, "equip / starter-items"),
    (0x490000, 0x4a0000, "npc schedule + worker walk"),
    (0x4a0000, 0x4b0000, "asset-load IO helper cluster (0x4af*)"),
    (0x4b0000, 0x4c0000, "battlefield render + wndproc"),
    (0x4c0000, 0x4d0000, "late/tail helpers"),
]


def band_of(va: int) -> str:
    for lo, hi, name in BANDS:
        if lo <= va < hi:
            return name
    return "other"


def main() -> int:
    funcs = {}
    with FUNCTIONS_CSV.open() as fh:
        for r in csv.DictReader(fh):
            try:
                va = int(r["entry"], 16)
            except (KeyError, ValueError):
                continue
            funcs[va] = {"name": r["name"], "size": int(r.get("size") or 0),
                         "thunk": r.get("is_thunk") == "true"}

    ledger = json.loads(LEDGER_JSON.read_text())
    touched = {int(e["va"], 16) for e in ledger["functions"] if e["status"] != "unported"}

    callees: dict[int, set[int]] = defaultdict(set)
    callers: dict[int, set[int]] = defaultdict(set)
    for f in BY_ADDR.glob("*.c"):
        try:
            cva = int(f.stem, 16)
        except ValueError:
            continue
        for m in FUN_RE.finditer(f.read_text(errors="replace")):
            callee = int(m.group(1), 16)
            if callee == cva:
                continue
            callees[cva].add(callee)
            callers[callee].add(cva)

    rows = []
    for va, info in funcs.items():
        if info["thunk"] or va in touched:
            continue
        pc = callers.get(va, set()) & touched
        if not pc:
            continue
        cs = callees.get(va, set())
        is_leaf = all(c in touched for c in cs)  # vacuously true for cs == set()
        rows.append({
            "va": va, "name": info["name"], "size": info["size"],
            "ported_callers": len(pc), "n_callees": len(cs),
            "unported_deps": len([c for c in cs if c not in touched and not funcs.get(c, {}).get("thunk")]),
            "leaf": is_leaf, "band": band_of(va),
        })

    rows.sort(key=lambda r: (-r["ported_callers"], r["size"]))
    leaves = [r for r in rows if r["leaf"]]

    L = []
    L.append("# OpenLords2 — port frontier (what to port next)")
    L.append("")
    L.append("> **DERIVED FILE** — `python3 tools/gen_port_ledger.py && "
             "python3 tools/gen_frontier.py`.")
    L.append("")
    L.append("The unported, non-thunk engine functions already **called by "
             "ported code** — the natural next chips. A **leaf** has all of its")
    L.append("own callees ported, so it can land today with zero new "
             "dependencies. Sorted by how many ported callers want it.")
    L.append("")
    L.append(f"- frontier functions: **{len(rows)}**")
    L.append(f"- of those, zero-dependency **leaves: {len(leaves)}** "
             f"(recommended order below)")
    L.append("")
    L.append("## Leaf shortlist — portable today (top 40 by ported-caller count)")
    L.append("")
    L.append("| VA | size | ported callers | band |")
    L.append("|----|-----:|---------------:|------|")
    for r in leaves[:40]:
        L.append(f"| 0x{r['va']:06x} | {r['size']} | {r['ported_callers']} | {r['band']} |")
    L.append("")
    L.append("## Full frontier by address band")
    L.append("")
    by_band: dict[str, list] = defaultdict(list)
    for r in rows:
        by_band[r["band"]].append(r)
    for lo, hi, name in BANDS:
        br = by_band.get(name, [])
        if not br:
            continue
        L.append(f"### {name} ({len(br)})")
        L.append("")
        L.append("| VA | size | ported callers | unported deps | leaf |")
        L.append("|----|-----:|---------------:|--------------:|:----:|")
        for r in sorted(br, key=lambda r: (-r["ported_callers"], r["size"])):
            L.append(f"| 0x{r['va']:06x} | {r['size']} | {r['ported_callers']} | "
                     f"{r['unported_deps']} | {'✓' if r['leaf'] else ''} |")
        L.append("")

    OUT_MD.write_text("\n".join(L) + "\n")
    print(f"wrote {OUT_MD.relative_to(REPO)}: {len(rows)} frontier, {len(leaves)} leaves")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
