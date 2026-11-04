#!/usr/bin/env python3
"""tools/bf/dedup_globals.py — a retail global two partitions generated (g_bf_<va> / g_rl_<va> defined in more than one
src/*_globals.c) gets ONE definition for the standalone: moved to src/rl_shared_globals.c as a byte array at the
largest extent any definition had (aligned 4; each partition's header keeps its typed view), and its duplicate
rhost aliases dropped (only the first alias header keeps it). Run after merging partitions; the standalone links
every src/*.c (rhost aliases instead: build.sh links no *_globals.c)."""
import collections
import glob
import os
import re

SRC = "src"
RH = "tools/rhost"
TY = {"uint8_t": 1, "int8_t": 1, "int16_t": 2, "uint16_t": 2, "int32_t": 4, "uint32_t": 4}
defre = re.compile(r"^(u?int(?:8|16|32)_t) (g_(?:bf|rl)_[0-9a-f]{6})(?:\[(0x[0-9a-f]+|\d+)\])?(?: __attribute__\(\(aligned\(4\)\)\))?( = [^;]*)?;\s*$", re.M)
shared = os.path.join(SRC, "rl_shared_globals.c")
defs = collections.defaultdict(list)   # sym → [(file, size, has_init)]
for fn in sorted(glob.glob(os.path.join(SRC, "*_globals.c"))):
    for m in defre.finditer(open(fn).read()):
        n = int(m.group(3), 0) if m.group(3) else 1
        defs[m.group(2)].append((fn, TY[m.group(1)] * n, bool(m.group(4))))
dups = {s: d for s, d in defs.items() if len(d) > 1}
if not dups:
    print("no duplicate globals")
    raise SystemExit(0)
size = {s: max(n for _, n, _ in d) for s, d in dups.items()}
inited = {s for s, d in dups.items() if any(i for _, _, i in d)}   # .data: the initial bytes from the exe itself
if inited:
    import sys
    sys.path.insert(0, os.path.dirname(__file__))
    import bfgraph as G
for fn in {f for d in dups.values() for f, _, _ in d}:
    t = open(fn).read()
    for s in dups:
        t = re.sub(r"^[^\n]*\b%s\b[^\n]*;\n" % s, "", t, flags=re.M)
    open(fn, "w").write(t)
lines = open(shared).read().rstrip("\n").split("\n") if os.path.exists(shared) else [
    "/* rl_shared_globals.c — the g_bf_ / g_rl_ globals more than one partition names, defined ONCE for the standalone",
    " * at the largest extent any view uses (each partition's header keeps its own typed view; rhost aliases them at their",
    " * VAs). NOT linked into rhost (build.sh skips *_globals.c). tools/bf/dedup_globals.py maintains it. */",
    "#include <stdint.h>", ""]
for s in sorted(size):
    if s in inited:
        b = G.rd(int(s[-6:], 16), size[s])
        lines.append(f"uint8_t {s}[{size[s]}] __attribute__((aligned(4))) = {{{','.join(str(x) for x in b)}}};")
    else:
        lines.append(f"uint8_t {s}[{size[s]}] __attribute__((aligned(4)));")
open(shared, "w").write("\n".join(lines) + "\n")
# rhost: one alias per symbol (the first header in sorted order keeps it)
seen = set()
for fn in sorted(glob.glob(os.path.join(RH, "rh_*_alias.h"))):
    t = open(fn).read()
    out = []
    for l in t.split("\n"):
        m = re.match(r"\s*BFA\((g_\w+),", l)
        if m and m.group(1) in seen:
            continue
        if m:
            seen.add(m.group(1))
        out.append(l)
    open(fn, "w").write("\n".join(out))
print(f"{len(dups)} duplicate(s) → {shared}: " + " ".join(f"{s}[{size[s]}]" for s in sorted(size)))
