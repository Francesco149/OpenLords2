#!/usr/bin/env python3
"""Generate a derived port ledger for OpenLords2.

The ledger maps every engine function (from the Ghidra export
``docs/decompiled/functions.csv``) to its port status, derived purely from
artifacts already in the tree — it is regenerated, never hand-maintained, so
it cannot drift:

  status     source of truth
  ------     ---------------
  tested     the VA appears as FUN_<va> in a src/ file whose module has a
             matching tests/test_<stem>.c  (port + host-suite coverage)
  ported     the VA appears as FUN_<va> in a src/ comment/provenance header
             but the module has no host test yet
  unported   present in the engine but never referenced from src/

OpenLords2 ports are verbatim translations of the Ghidra decompile; each
ported function carries a ``Port of FUN_<va> @ 0xVA`` provenance comment in
src/.  That `FUN_<va>` token is the single machine-readable signal this
generator keys on (mirrors openrecet's ledger, minus its runtime
CALL_TRACE tier — OpenLords2 verifies via the host unit suite + the
bit-exact DDraw frame-diff harness rather than call-trace probes).

Outputs (all under docs/, all git-tracked — they contain no vendor bytes):
  docs/port-ledger.json   full per-VA map (machine-readable)
  docs/port-ledger.md     human-readable summary table + per-status VA lists
  docs/STATUS.md          60-second headline: counts, %, current phase, blocker

Run from the repo root:  python3 tools/gen_port_ledger.py
Add --check to fail (exit 3) if the on-disk ledger is stale (for a pre-commit
hook): it regenerates into memory and diffs against what's committed.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src"
TESTS = REPO / "tests"
FUNCTIONS_CSV = REPO / "docs" / "decompiled" / "functions.csv"

LEDGER_JSON = REPO / "docs" / "port-ledger.json"
LEDGER_MD = REPO / "docs" / "port-ledger.md"
STATUS_MD = REPO / "docs" / "STATUS.md"

FRONT_MD = REPO / "docs" / "FRONT.md"
EVIDENCE_JSON = REPO / "docs" / "rhost-evidence.json"


def front_block() -> str:
    """docs/FRONT.md's status block (between the status:begin / status:end markers), verbatim."""
    try:
        t = FRONT_MD.read_text()
        a, b = t.index("<!-- status:begin -->"), t.index("<!-- status:end -->")
        return t[a + len("<!-- status:begin -->"):b].strip()
    except (OSError, ValueError):
        return "(docs/FRONT.md has no status block)"


def evidence_block() -> str:
    """the runtime axis: functions proven against retail on identical state (rhost.py verify)."""
    try:
        ev = json.loads(EVIDENCE_JSON.read_text())
    except (OSError, ValueError):
        return "No rhost evidence recorded yet (`python3 tools/rhost/rhost.py verify`)."
    hooks, hyb = ev.get("hooks", ev), ev.get("hybrid")
    # reached = shadowed calls, or (RH_SH_HOST rows: host level, never shadowed — the hybrid pass is their evidence) sampled ones
    ok = {k: v for k, v in hooks.items() if (v.get("calls", 0) > 0 or v.get("sampled", 0) > 0) and v.get("diverge", 1) == 0}
    bad = {k: v for k, v in hooks.items() if v.get("diverge", 0) > 0}
    rows = [f"| `{k}` | `{v.get('va', '?')}` | {v.get('depth', '?')} | {v['calls']} | {v['scenarios']} | {v['diverge']} | "
            f"{v.get('date', '?')} |" for k, v in sorted(hooks.items())]
    hline = ""
    if hyb:
        hline = (f"Hybrid (all {hyb['hooks']} hooks replaced at once, {hyb['target']}, {hyb['date']}): corpus --check "
                 + ("**PASS** on all " + str(hyb["corpus"]) + " scenarios." if not hyb["failing"]
                    else f"**FAIL** on {', '.join(hyb['failing'])}.") + "\n\n")
    return (f"**{len(ok)} function(s) rhost-verified** (shadow: 0 divergences over the corpus), "
            f"{len(bad)} diverging, {len(hooks) - len(ok) - len(bad)} never called, of {len(hooks)} hooked. "
            "Depth = the shadow pass that reached the hook (a hook nested in another runs retail there).\n\n" + hline +
            "| hook | retail VA | depth | calls | scenarios | diverge | date |\n|---|---|---:|---:|---:|---:|---|\n" + "\n".join(rows))


FUN_RE = re.compile(r"FUN_([0-9a-fA-F]{6,8})")


def load_engine_functions() -> dict[int, dict]:
    """All engine functions from the Ghidra export, keyed by entry VA."""
    funcs: dict[int, dict] = {}
    with FUNCTIONS_CSV.open() as fh:
        for row in csv.DictReader(fh):
            try:
                va = int(row["entry"], 16)
            except (KeyError, ValueError):
                continue
            funcs[va] = {
                "name": row.get("name", ""),
                "size": int(row.get("size") or 0),
                "is_thunk": (row.get("is_thunk") == "true"),
            }
    return funcs


def tested_modules() -> set[str]:
    """src module stems that have a matching tests/test_<stem>.c."""
    stems = set()
    for t in TESTS.glob("test_*.c"):
        stems.add(t.stem[len("test_"):])
    return stems


def scan_src(tested: set[str]) -> dict[int, dict]:
    """Map each FUN_<va> referenced in src/ to the files that reference it and
    whether any of those files is a test-covered module."""
    refs: dict[int, set[str]] = {}
    for path in sorted(SRC.rglob("*.c")) + sorted(SRC.rglob("*.h")):
        rel = str(path.relative_to(REPO))
        text = path.read_text(errors="replace")
        for m in FUN_RE.finditer(text):
            refs.setdefault(int(m.group(1), 16), set()).add(rel)
    out: dict[int, dict] = {}
    for va, files in refs.items():
        is_tested = any(Path(f).stem in tested for f in files)
        out[va] = {"src": sorted(files), "tested": is_tested}
    return out


def classify(funcs, refs):
    """Assign one status per engine VA, plus collect 'ported but not an engine
    function' VAs (indirect/vtable targets or sub-helper labels)."""
    entries: dict[int, dict] = {}
    known = set(funcs)
    for va, info in funcs.items():
        ref = refs.get(va)
        if ref is None:
            status, files = "unported", []
        elif ref["tested"]:
            status, files = "tested", ref["src"]
        else:
            status, files = "ported", ref["src"]
        entries[va] = {
            "va": f"0x{va:06x}",
            "name": info["name"],
            "size": info["size"],
            "is_thunk": info["is_thunk"],
            "status": status,
            "src": files,
        }
    orphans = sorted(set(refs) - known)
    return entries, orphans


def summarize(entries, funcs, orphans):
    real = {va: e for va, e in entries.items() if not e["is_thunk"]}
    by = lambda s: sum(1 for e in real.values() if e["status"] == s)
    counts = {
        "engine_functions_total": len(funcs),
        "non_thunk_functions": len(real),
        "tested": by("tested"),
        "ported": by("ported"),
        "unported": by("unported"),
        "orphan_refs_not_in_function_table": len(orphans),
    }
    touched = counts["tested"] + counts["ported"]
    counts["touched"] = touched
    counts["pct_touched"] = round(100.0 * touched / max(1, counts["non_thunk_functions"]), 1)
    counts["pct_tested"] = round(100.0 * counts["tested"] / max(1, counts["non_thunk_functions"]), 1)
    # Bytes-covered: a better progress signal than function count, since the
    # engine has a long tail of tiny leaves and a few huge drivers.
    tot_bytes = sum(e["size"] for e in real.values())
    touched_bytes = sum(e["size"] for e in real.values() if e["status"] != "unported")
    counts["bytes_total"] = tot_bytes
    counts["bytes_touched"] = touched_bytes
    counts["pct_bytes_touched"] = round(100.0 * touched_bytes / max(1, tot_bytes), 1)
    return counts


def render_json(entries, orphans, counts) -> str:
    payload = {
        "_generated_by": "tools/gen_port_ledger.py",
        "_note": "DERIVED FILE — do not edit by hand; run the generator. "
                 "Output is a pure function of src/ + tests/ + functions.csv, "
                 "so it is idempotent (safe for a pre-commit --check).",
        "counts": counts,
        "orphan_refs": [f"0x{va:06x}" for va in orphans],
        "functions": [entries[va] for va in sorted(entries)],
    }
    return json.dumps(payload, indent=1) + "\n"


def render_status(counts) -> str:
    c = counts
    bar_n = round(c["pct_touched"] / 5)  # 20-cell bar
    bar = "█" * bar_n + "░" * (20 - bar_n)
    return f"""# OpenLords2 — status at a glance

> **DERIVED FILE** — regenerate with `python3 tools/gen_port_ledger.py`
> (also auto-regenerated by the pre-commit hook). Headline numbers only;
> narrative lives in `PROGRESS.md`, durable RE in `findings/`, full map in
> `port-ledger.md`.

## Port coverage (non-thunk engine functions)

```
{bar}  {c['pct_touched']}% touched   ({c['pct_tested']}% host-tested, {c['pct_bytes_touched']}% of code bytes)
```

| status      | count | what it means                                          |
|-------------|------:|--------------------------------------------------------|
| tested      | {c['tested']:>5} | ported + module covered by the host unit suite       |
| ported      | {c['ported']:>5} | reimplemented in src/, no host test for that module  |
| **touched** | **{c['touched']:>3}** | tested + ported (FUN_ provenance ref in src/)    |
| unported    | {c['unported']:>5} | exists in engine, never referenced from src/         |
| **total**   | **{c['non_thunk_functions']:>3}** | non-thunk engine functions (of {c['engine_functions_total']} incl. thunks) |

Code-byte coverage ({c['pct_bytes_touched']}%) is the truer progress signal: the engine
has a long tail of tiny leaf helpers, so function count understates how much
of the actual instruction volume is ported.

{c['orphan_refs_not_in_function_table']} VAs are referenced in src/ but absent from the function table
(indirect/vtable targets or sub-helper labels) — see `port-ledger.json` `orphan_refs`.

## Runtime verification (rhost — the truth axis since 2026-10-23)

{evidence_block()}

"Touched" above counts `FUN_` references in `src/`; it is not evidence. Earlier "BYTE-EXACT" / "1:1"
claims are unverified until rhost re-checks them (`docs/audits/2026-10-23-methodology-reset.md`).

## Current front (from `docs/FRONT.md`)

{front_block()}

## Where to read next

- `STATUS.md` (this file) — 60-second orientation.
- `port-ledger.md` / `.json` — per-function port status (derived).
- `port-frontier.md` — what to port next: unported fns reachable from ported
  code, zero-dep leaves ranked (derived; `tools/gen_frontier.py`).
- `ROADMAP.md` — next chips with subsystem / difficulty / target module.
- `PROGRESS.md` — dated narrative changelog.
- `findings/INDEX.md` — map of subsystem RE writeups.
- `harness-roadmap.md` — verification/tooling phases.
- `AGENT-WORKFLOW.md` — how to work on this repo.
- `PLAN.md` — goal, constraints, phased roadmap.
"""


def render_md(entries, orphans, counts) -> str:
    c = counts
    lines = [
        "# OpenLords2 — port ledger",
        "",
        "> **DERIVED FILE** — regenerate with `python3 tools/gen_port_ledger.py`.",
        "> See `STATUS.md` for the headline.",
        "",
        "Per-engine-function port status, derived from `functions.csv` (universe)",
        "and `FUN_<va>` provenance references in `src/` (each port carries a",
        "`Port of FUN_<va>` comment). A function is **tested** when its src module",
        "has a matching `tests/test_<stem>.c`. This is the answer to",
        "*\"is FUN_x done?\"* at a glance.",
        "",
        "## Summary",
        "",
        f"- non-thunk engine functions: **{c['non_thunk_functions']}** "
        f"(of {c['engine_functions_total']} incl. thunks)",
        f"- touched: **{c['touched']}** ({c['pct_touched']}%) — "
        f"tested {c['tested']}, ported {c['ported']}",
        f"- code bytes touched: **{c['pct_bytes_touched']}%** "
        f"({c['bytes_touched']:,} / {c['bytes_total']:,} B)",
        f"- unported: **{c['unported']}**",
        f"- orphan refs (in src/, not in function table): {c['orphan_refs_not_in_function_table']}",
        "",
    ]
    real = [entries[va] for va in sorted(entries) if not entries[va]["is_thunk"]]
    for status, blurb in (
        ("tested", "ported + host unit suite"),
        ("ported", "reimplemented, no host test yet"),
    ):
        rows = [e for e in real if e["status"] == status]
        lines.append(f"## {status} ({len(rows)}) — {blurb}")
        lines.append("")
        lines.append("| VA | name | size | src |")
        lines.append("|----|------|-----:|-----|")
        for e in rows:
            src = ", ".join(Path(s).name for s in e["src"][:3])
            if len(e["src"]) > 3:
                src += f" (+{len(e['src']) - 3})"
            lines.append(f"| {e['va']} | {e['name']} | {e['size']} | {src} |")
        lines.append("")
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="exit 3 if on-disk ledger differs from a fresh gen")
    args = ap.parse_args()

    funcs = load_engine_functions()
    refs = scan_src(tested_modules())
    entries, orphans = classify(funcs, refs)
    counts = summarize(entries, funcs, orphans)

    out = {
        LEDGER_JSON: render_json(entries, orphans, counts),
        LEDGER_MD: render_md(entries, orphans, counts),
        STATUS_MD: render_status(counts),
    }

    if args.check:
        stale = [p.name for p, txt in out.items()
                 if not p.exists() or p.read_text() != txt]
        if stale:
            print(f"port ledger stale: {', '.join(stale)} "
                  f"(run: python3 tools/gen_port_ledger.py)", file=sys.stderr)
            return 3
        print("port ledger up to date")
        return 0

    for path, txt in out.items():
        path.write_text(txt)
    print(f"wrote {LEDGER_JSON.name}, {LEDGER_MD.name}, {STATUS_MD.name}")
    print(f"  {counts['touched']}/{counts['non_thunk_functions']} touched "
          f"({counts['pct_touched']}%), {counts['tested']} tested, "
          f"{counts['unported']} unported, "
          f"{counts['pct_bytes_touched']}% of code bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
