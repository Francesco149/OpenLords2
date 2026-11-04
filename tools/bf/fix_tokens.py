#!/usr/bin/env python3
"""tools/bf/fix_tokens.py [files] — in the battlefield port's sources, a FUN_00xxxxxx token outside a
"Port of FUN_00xxxxxx" provenance line becomes bare hex 0xxxxxxx (the ledger counts every FUN_ token in src/
as ported: tools/audit_provenance.py). Default: src/engine_bf_*.[ch]."""
import glob
import re
import sys

files = sys.argv[1:] or sorted(glob.glob("src/engine_bf_*.[ch]"))
tot = 0
for fn in files:
    s = open(fn).read()
    n = [0]

    def fix(m, s=s):
        if s[max(0, m.start() - 8):m.start()] == "Port of ":
            return m.group(0)
        n[0] += 1
        return "0x" + m.group(1)
    s2 = re.sub(r"FUN_00([0-9a-f]{6})\b", fix, s)
    if n[0]:
        open(fn, "w").write(s2)
        print(f"{fn}: {n[0]}")
        tot += n[0]
print(f"{tot} token(s) → bare VA")
