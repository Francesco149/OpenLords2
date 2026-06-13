#!/usr/bin/env python3
"""Unit tests for the trace op parser / compiler / lint (trace_ops).

Run: nix develop -c python3 tools/trace_studio_v3/test_trace_ops.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import trace_ops as ops  # noqa: E402

FAILS = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global FAILS
    if cond:
        print(f"ok   {name}")
    else:
        FAILS += 1
        print(f"FAIL {name}  {detail}")


def codes(findings, level=None):
    return [f["code"] for f in findings
            if level is None or f["level"] == level]


# ── parse ─────────────────────────────────────────────────────────────────────
o = ops.parse([
    "# comment",
    "",
    '{"t":5,"m":512,"l":3276980}',
    '{"wait":"TITLE"}',
    '{"capture":10,"label":"menu"}',
    '{"caprange":[20,16],"label":"map","stride":4}',
    '{"wait_until":{"va":123,"type":"u8","op":"==","val":0}}',
    "not json at all",
])
check("parse kinds",
      [x["_kind"] for x in o] == ["comment", "comment", "input", "wait",
                                  "capture", "caprange", "wait_until",
                                  "unknown"],
      str([x["_kind"] for x in o]))

# ── compile: caprange expansion, offset labels, segment tracking ─────────────
compiled, plan = ops.compile_ops(o)
check("plan size", len(plan) == 1 + 4, str(len(plan)))
check("range labels", [p["label"] for p in plan[1:]] ==
      ["map.0000", "map.0004", "map.0008", "map.0012"],
      str([p["label"] for p in plan[1:]]))
check("range n anchor-relative", [p["n"] for p in plan[1:]] ==
      [20, 24, 28, 32], str([p["n"] for p in plan[1:]]))
check("seg index", plan[0]["seg"] == 1 and plan[1]["seg"] == 1,
      str([p["seg"] for p in plan]))
check("groups", plan[0]["group"] is None and plan[1]["group"] == "map")
n_engine = sum(1 for ln in compiled
               if ln.strip() and not ln.strip().startswith("#"))
check("compiled op count", n_engine == 1 + 1 + 1 + 1 + 4, str(n_engine))
check("compiled is primitive",
      not any("caprange" in ln for ln in compiled
              if not ln.strip().startswith("#")))

# drill (stride 1) labels are a superset sharing overview labels
o2 = ops.parse(['{"wait":"TITLE"}',
                '{"caprange":[20,16],"label":"map"}'])
_, plan2 = ops.compile_ops(o2)
check("drill superset", {p["label"] for p in plan}.issubset(
    {p["label"] for p in plan2} | {"menu"}),
    str([p["label"] for p in plan2]))

# ── lint ─────────────────────────────────────────────────────────────────────
f = ops.lint(o, "both")
check("lint flags unknown", "unknown-op" in codes(f, "error"), str(f))
check("lint warns wait_until on both", "wait-until-port" in codes(f, "warn"))

f = ops.lint(ops.parse(['{"wait":"NOPE"}']), "both")
check("lint bad anchor", "bad-anchor" in codes(f, "error"))

f = ops.lint(ops.parse(['{"wait":"LOADING_END"}',
                        '{"capture":1,"label":"x"}']), "both")
check("lint port anchor gap", "port-anchor-gap" in codes(f, "warn"))
f = ops.lint(ops.parse(['{"wait":"LOADING_END"}',
                        '{"capture":1,"label":"x"}']), "retail")
check("retail-only no anchor gap", "port-anchor-gap" not in codes(f))

f = ops.lint(ops.parse(['{"capture":5,"label":"a"}']), "both")
check("lint boot-relative", "boot-relative" in codes(f, "warn"))

f = ops.lint(ops.parse(['{"wait":"TITLE"}', '{"capture":5}']), "both")
check("lint no label", "no-label" in codes(f, "error"))

f = ops.lint(ops.parse(['{"wait":"TITLE"}',
                        '{"capture":5,"label":"sp ace"}']), "both")
check("lint bad label", "bad-label" in codes(f, "error"))

f = ops.lint(ops.parse(['{"wait":"TITLE"}',
                        '{"capture":5,"label":"a"}',
                        '{"capture":9,"label":"a"}']), "both")
check("lint dup label", "dup-label" in codes(f, "error"))

f = ops.lint(ops.parse(['{"wait":"TITLE"}',
                        '{"caprange":[0,9000],"label":"big"}']), "both")
check("lint op overflow", "op-overflow" in codes(f, "error"))

f = ops.lint(ops.parse(['{"wait":"TITLE"}',
                        '{"t":9,"m":512}', '{"t":4,"m":512}']), "both")
check("lint t order", "t-order" in codes(f, "warn"))

f = ops.lint(ops.parse(['{"wait":"TITLE"}']), "both")
check("lint no captures", "no-captures" in codes(f, "warn"))

# label too long for the port cap
long = "x" * 30
f = ops.lint(ops.parse([f'{{"wait":"TITLE"}}',
                        f'{{"caprange":[0,8],"label":"{long}"}}']), "both")
check("lint generated label len", "label-len" in codes(f, "error"))

print(f"\n{'ALL PASS' if FAILS == 0 else f'{FAILS} FAILURES'}")
sys.exit(1 if FAILS else 0)
