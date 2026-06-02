#!/usr/bin/env python3
"""
save_diff.py — schema-aware byte diff of two Lords2 .sav files.

A .sav file is a flat concatenation of the 225-entry save-schema table
(block 1, see docs/findings/save-schema-table.csv) followed by 16 castle
blocks of 12800 B each (block 2).  Because the layout is positional, any
differing byte maps unambiguously to a schema entry (or a castle block),
which maps to a retail global VA.  This tool localises differences to
those entries so a state divergence (e.g. the new-game turn/phase bug,
project_new_game_render Phase C) can be pinpointed to a specific global
instead of an opaque byte offset.

Usage:
  nix develop -c python3 tools/save_diff.py A.sav B.sav
  nix develop -c python3 tools/save_diff.py A.sav B.sav --only 3,13-40
  nix develop -c python3 tools/save_diff.py A.sav B.sav --hexdump 3

Output: one line per schema entry (or castle block) that differs, with
the entry index, retail VA, size, byte-diff count, and the first few
differing (offset: A→B) byte pairs.  --hexdump dumps full A/B bytes for
the given entry index.
"""
import argparse
import csv
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CSV = REPO / "docs/findings/save-schema-table.csv"
CASTLE_BLOCK_BYTES = 12800
CASTLE_BLOCK_COUNT = 16


def load_schema():
    entries = []
    with open(CSV) as f:
        for row in csv.DictReader(f):
            entries.append({
                "idx": int(row["idx"]),
                "off": int(row["sav_offset"]),
                "va": row["va"],
                "size": int(row["size"]),
                "section": row["section"],
            })
    # Append the 16 castle blocks as synthetic entries.
    base = entries[-1]["off"] + entries[-1]["size"]
    for b in range(CASTLE_BLOCK_COUNT):
        entries.append({
            "idx": 1000 + b,
            "off": base + b * CASTLE_BLOCK_BYTES,
            "va": f"castle_block_{b+1}",
            "size": CASTLE_BLOCK_BYTES,
            "section": ".castles",
        })
    return entries


def parse_only(spec, max_idx):
    if not spec:
        return None
    keep = set()
    for part in spec.split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-")
            keep.update(range(int(a), int(b) + 1))
        else:
            keep.add(int(part))
    return keep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--only", help="comma list / ranges of entry idx to include")
    ap.add_argument("--hexdump", type=int, default=None,
                    help="dump full A/B bytes for this entry idx")
    ap.add_argument("--max-pairs", type=int, default=8,
                    help="max differing byte pairs to print per entry")
    args = ap.parse_args()

    a = Path(args.a).read_bytes()
    b = Path(args.b).read_bytes()
    schema = load_schema()
    only = parse_only(args.only, len(schema))

    print(f"A={args.a} ({len(a)} B)  B={args.b} ({len(b)} B)")
    if len(a) != len(b):
        print(f"!! length mismatch: {len(a)} vs {len(b)}")

    total_diff_entries = 0
    total_diff_bytes = 0
    for e in schema:
        if only is not None and e["idx"] not in only:
            continue
        o, sz = e["off"], e["size"]
        sa, sb = a[o:o + sz], b[o:o + sz]
        if sa == sb:
            continue
        diffs = [(i, sa[i], sb[i]) for i in range(min(len(sa), len(sb)))
                 if sa[i] != sb[i]]
        total_diff_entries += 1
        total_diff_bytes += len(diffs)

        if args.hexdump is not None and e["idx"] == args.hexdump:
            print(f"\n=== entry {e['idx']} va={e['va']} size={sz} "
                  f"off={o} ({len(diffs)} bytes differ) ===")
            print(f"A: {sa.hex()}")
            print(f"B: {sb.hex()}")
            continue

        pairs = " ".join(f"+{i}:{x:02x}->{y:02x}"
                         for i, x, y in diffs[:args.max_pairs])
        more = f" (+{len(diffs)-args.max_pairs} more)" if len(diffs) > args.max_pairs else ""
        print(f"idx {e['idx']:>4} va {e['va']:<14} sz {sz:<6} "
              f"diff {len(diffs):>6}B  {pairs}{more}")

    print(f"\n{total_diff_entries} entries differ, {total_diff_bytes} bytes total")


if __name__ == "__main__":
    main()
