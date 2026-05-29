#!/usr/bin/env python3
"""Flag ledger entries that may be FUN_-token *provenance pollution*.

Background — the "FUN_ token gotcha"
------------------------------------
`tools/gen_port_ledger.py` derives a function's port status from a single
machine-readable signal: does its ``FUN_<va>`` token appear anywhere in
``src/``?  That keys on the convention that the token is only ever written
in a port's *provenance* comment ("Port of FUN_… @ 0x…").

The trap: a comment in a *ported* module that mentions an *unported*
neighbour by its ``FUN_<va>`` token (a TODO list, a "retail calls FUN_…"
note, a "surveyed: …" survey line, a call-chain diagram) silently promotes
that neighbour to *touched* — and to *tested* if the module has a host
test.  One stray token inflates both the function count and the
code-byte coverage headline.

This auditor does NOT change the ledger.  It is a *warn-only* review aid:
it re-derives, for every touched (tested|ported) function, whether any of
its ``src/`` references looks like genuine self-provenance, and lists the
ones that don't so a human can eyeball them.

Why it can't just auto-demote
-----------------------------
There is no fully reliable syntactic separator.  Genuine provenance is
written many ways in this tree:

    Port of FUN_…            port of retail FUN_…      from retail FUN_…
    Mirror of retail FUN_…   Equivalent to FUN_… inlined
    FUN_… @ 0x<va> → name    section header "FUN_… (NNN B) — …" + code below
    inlined helpers whose token also appears in the .c body

…and pollution is also written many ways (call-pattern ``FUN_…(args)``,
size lists, "surveyed/deferred/unported/unknown", call-chain arrows).
The *callish-only* signal (every reference is a ``FUN_<va>(`` call form,
never anything else) is the one high-precision tell and is reported
separately; everything else in the suspect list needs the reviewer to
confirm the function is genuinely unimplemented before demoting (replace
the comment token with bare hex ``0x<va>``).

Usage:  python3 tools/audit_provenance.py      (from repo root)
"""

from __future__ import annotations

import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src"
LEDGER = REPO / "docs" / "port-ledger.json"

TOK = re.compile(r"FUN_([0-9a-fA-F]{6,8})")


def collect_refs() -> dict[int, list[tuple[str, int, str]]]:
    refs: dict[int, list[tuple[str, int, str]]] = {}
    for path in sorted(SRC.glob("*.c")) + sorted(SRC.glob("*.h")):
        for i, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            for m in TOK.finditer(line):
                refs.setdefault(int(m.group(1), 16), []).append((path.name, i, line))
    return refs


def has_self_provenance(va: int, lines: list[tuple[str, int, str]]) -> bool:
    """A reference that anchors the token to the function it ports."""
    for _, _, line in lines:
        # canonical "FUN_<va> @ 0x<va>" provenance map
        if re.search(r"@\s*0x0*%x\b" % va, line):
            return True
        # prose provenance: (port|mirror|equivalent) [of|to] [the] [retail] FUN_<va>
        if re.search(
            r"(port|mirror|equivalent)\w*\s+(of|to)\s+(the\s+)?(retail\s+)?`?FUN_0*%x" % va,
            line,
            re.I,
        ):
            return True
        if re.search(r"from\s+retail\s+`?FUN_0*%x" % va, line, re.I):
            return True
    return False


def is_callish_only(va: int, lines: list[tuple[str, int, str]]) -> bool:
    return all(re.search(r"FUN_0*%x\s*\(" % va, line) for _, _, line in lines)


def main() -> int:
    ledger = json.loads(LEDGER.read_text())
    touched = {
        int(f["va"], 16): f
        for f in ledger["functions"]
        if f["status"] in ("tested", "ported") and not f["is_thunk"]
    }
    refs = collect_refs()

    callish: list[tuple[int, dict]] = []
    suspect: list[tuple[int, dict]] = []
    for va, f in touched.items():
        lines = refs.get(va)
        if not lines or has_self_provenance(va, lines):
            continue
        (callish if is_callish_only(va, lines) else suspect).append((va, f))

    def dump(title: str, rows: list[tuple[int, dict]]) -> None:
        byts = sum(f["size"] for _, f in rows)
        print(f"\n## {title}: {len(rows)} funcs, {byts} bytes")
        for va, f in sorted(rows, key=lambda x: -x[1]["size"]):
            fn, ln, line = refs[va][0]
            print(f"  0x{va:06x} {f['size']:>6}B {f['status']:<7} {fn}:{ln}: {line.strip()[:64]}")

    dump("HIGH-CONFIDENCE pollution (callish-only — safe to demote)", callish)
    dump("REVIEW (no self-provenance — confirm unimplemented before demoting)", suspect)
    print(
        f"\n{len(touched)} touched total; {len(callish)} callish-only + "
        f"{len(suspect)} review = {len(callish) + len(suspect)} flagged.\n"
        "Auditor is advisory; it does not modify the ledger."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
