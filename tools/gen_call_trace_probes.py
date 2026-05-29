#!/usr/bin/env python3
"""
tools/gen_call_trace_probes.py — Phase G.2 helper for the call-graph diff.

Two jobs, both driven off docs/port-ledger.json + the Ghidra decompile:

  1. ANNOTATION WORKLIST (default).  Lists every ported/tested function and
     whether its src already carries a CALL_TRACE_ENTER(0x<va>) probe.  This
     is the worklist for the manual annotation pass — each unprobed function
     gets one `CALL_TRACE_ENTER(0xVA);` line at its top (the insertion stays
     manual so the diff is reviewable; this tool just finds the targets).

  2. RETAIL HOOK-VA SET (`--hook-vas OUT.json`).  Emits the VA list to feed
     `frida_capture.py --call-trace-vas @OUT.json`.  The set is:

         ported/tested VAs  ∪  their decompiled callees (one hop out)

     The callee union is what makes the diff useful: if we only hooked the
     functions we've already ported, the retail trace's "retail-only" set
     would be empty by construction (we hook only things we ported, retail
     calls them too → all overlap).  Hooking the *callees* of ported
     functions means "retail-only" = functions retail invokes from inside a
     ported caller that we haven't ported yet — i.e. the port-side gap list.

Callees come from the per-function decompile under
docs/decompiled/by-address/<va>.c (regex over `FUN_<hex>` references), so no
objdump / live binary is needed.

Usage:
    nix develop -c python3 tools/gen_call_trace_probes.py            # worklist
    nix develop -c python3 tools/gen_call_trace_probes.py --hook-vas runs/hookvas.json
    nix develop -c python3 tools/gen_call_trace_probes.py --hook-vas runs/hookvas.json \\
        --only src/engine.c          # restrict to functions ported in one TU

Exit code: 0 always (diagnostic / generator).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT        = Path(__file__).resolve().parent.parent
LEDGER_JSON = ROOT / "docs" / "port-ledger.json"
BY_ADDR_DIR = ROOT / "docs" / "decompiled" / "by-address"

# Functions whose status means "we have an implementation" — candidates for
# annotation and the base of the hook-VA set.
PORTED_STATUSES = {"ported", "tested", "verified", "done"}

FUN_RE = re.compile(r"FUN_([0-9a-fA-F]{5,8})")
# Match an existing CALL_TRACE_ENTER(0x4xxxxx) or _STUB probe for a given VA.
PROBE_RE_TMPL = r"CALL_TRACE_ENTER(?:_STUB)?\s*\(\s*0x0*{va:x}\s*\)"


def _va_int(v) -> int:
    return int(v, 16) if isinstance(v, str) else int(v)


def load_ported() -> list[dict]:
    ledger = json.loads(LEDGER_JSON.read_text())
    fns = ledger["functions"]
    out = []
    for fn in fns:
        if fn.get("status") in PORTED_STATUSES and fn.get("src"):
            out.append({
                "va":     _va_int(fn["va"]),
                "name":   fn.get("name", ""),
                "status": fn.get("status", ""),
                "src":    [s for s in fn["src"] if s.endswith(".c")],
            })
    out.sort(key=lambda f: f["va"])
    return out


def has_probe(va: int, src_files: list[str]) -> bool:
    """True if any of the function's .c src files already references a
    CALL_TRACE_ENTER probe for this VA."""
    pat = re.compile(PROBE_RE_TMPL.format(va=va))
    for rel in src_files:
        p = ROOT / rel
        if p.exists() and pat.search(p.read_text()):
            return True
    return False


def callees_of(va: int) -> set[int]:
    """VAs of functions called inside `va`'s decompiled body."""
    f = BY_ADDR_DIR / f"{va:x}.c"
    if not f.exists():
        return set()
    out = set()
    for m in FUN_RE.finditer(f.read_text()):
        callee = int(m.group(1), 16)
        if callee != va:
            out.add(callee)
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hook-vas", type=Path, default=None, metavar="OUT.json",
                    help="write the retail hook-VA set (ported ∪ callees) as "
                         "a JSON {vas:[...]} file for --call-trace-vas @OUT")
    ap.add_argument("--only", default=None, metavar="src/foo.c",
                    help="restrict to functions whose src includes this file")
    ap.add_argument("--no-callees", action="store_true",
                    help="hook-vas: emit only the ported VAs, not their "
                         "callees (diff will show overlap only, no gaps)")
    args = ap.parse_args(argv)

    ported = load_ported()
    if args.only:
        ported = [f for f in ported if args.only in f["src"]]

    if args.hook_vas is None:
        # Annotation worklist.
        n_probed = 0
        print(f"# Call-trace annotation worklist — {len(ported)} ported/tested "
              f"functions with src")
        print(f"# {'va':>10}  {'probe':5}  {'status':7}  name  ←  src")
        for f in ported:
            probed = has_probe(f["va"], f["src"])
            n_probed += probed
            mark = "  ✓  " if probed else "  ·  "
            print(f"  0x{f['va']:08x} {mark} {f['status']:7}  "
                  f"{f['name']}  ←  {', '.join(f['src'])}")
        print()
        print(f"# {n_probed}/{len(ported)} already carry a CALL_TRACE_ENTER probe; "
              f"{len(ported) - n_probed} to annotate.")
        return 0

    # Hook-VA set.
    ported_vas = {f["va"] for f in ported}
    hook = set(ported_vas)
    callee_only = set()
    if not args.no_callees:
        for va in ported_vas:
            for c in callees_of(va):
                hook.add(c)
                if c not in ported_vas:
                    callee_only.add(c)

    vas_sorted = sorted(hook)
    out = {
        "_generated_by": "tools/gen_call_trace_probes.py",
        "n_ported":      len(ported_vas),
        "n_callee_only": len(callee_only),
        "n_total":       len(vas_sorted),
        "vas":           [f"0x{v:x}" for v in vas_sorted],
    }
    args.hook_vas.parent.mkdir(parents=True, exist_ok=True)
    args.hook_vas.write_text(json.dumps(out, indent=2))
    print(f"# hook-VA set → {args.hook_vas}")
    print(f"#   ported/tested : {len(ported_vas)}")
    print(f"#   callee-only   : {len(callee_only)}  "
          f"(unported neighbours — the gap candidates)")
    print(f"#   total to hook : {len(vas_sorted)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
