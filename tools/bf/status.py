#!/usr/bin/env python3
"""tools/bf/status.py — the battlefield port's progress per partition (docs/findings/battlefield-sim-port.md):
functions ported (defined in src/engine_bf_<part>.c), still pending (a stub in _pending.c), hooked (a row in
tools/rhost/rh_hooks_bf_<part>.c), and the corpus coverage of what is left. -v: list the pending functions."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import bfgraph as G
from parts import PARTS, FAMILY

cov = G.coverage()
hooked = G.hooked()
src = {}
for p in PARTS:
    def rd(f):
        f = os.path.join(G.SRC, f)
        return open(f).read() if os.path.exists(f) else ""
    fam = FAMILY.get(p, "bf")
    stem = f"engine_bf_{p}" if fam == "bf" else f"rl_{p}"
    defined = {int(v, 16) for v in re.findall(r"^(?:void|int|unsigned|uint32_t|int32_t) %s_([0-9a-f]{6})\(" % fam, rd(f"{stem}.c"), re.M)}
    pending = {int(v, 16) for v in re.findall(r"^int %s_([0-9a-f]{6})\(" % fam, rd(f"{stem}_pending.c"), re.M)}
    src[p] = (defined, pending)
tot = [0] * 6
print(f"{'part':8s} {'funcs':>5s} {'ported':>6s} {'hooked':>6s} {'pending':>7s} {'bytes left':>10s} {'left reached':>12s}")
for p, vs in PARTS.items():
    d, pe = src[p]
    left = [v for v in vs if v in pe]
    row = [len(vs), len(d & set(vs)), sum(1 for v in vs if v in hooked), len(left),
           sum(G.F[v]["size_i"] for v in left), sum(1 for v in left if cov.get(v, 0))]
    tot = [a + b for a, b in zip(tot, row)]
    print(f"{p:8s} {row[0]:5d} {row[1]:6d} {row[2]:6d} {row[3]:7d} {row[4]:10d} {row[5]:12d}")
    if "-v" in sys.argv and left:
        print("         " + " ".join(f"{v:06x}({G.F[v]['size']},c{cov.get(v, 0)})" for v in left))
    lost = [v for v in vs if v not in d and v not in pe]
    if lost:
        print(f"         NEITHER ported nor pending: {' '.join(f'{v:06x}' for v in lost)}")
print(f"{'total':8s} {tot[0]:5d} {tot[1]:6d} {tot[2]:6d} {tot[3]:7d} {tot[4]:10d} {tot[5]:12d}")
