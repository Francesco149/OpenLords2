#!/usr/bin/env python3
"""blocks.py — retail's basic blocks from the IDA listing (decompiled-ida/all_ida.asm): leaders = a function's entry,
every jump target inside it, and the instruction after a jump / return. → [(va, size, function va)] for block coverage
(`rd cov --blocks`: rhost --coverage arms a one-shot INT3 at each leader)."""
import os
import re

ASM = os.path.join(os.environ.get("OL2_EPHEMERAL", "/mnt/ephemeral/openlords2"), "decompiled-ida", "all_ida.asm")
HDR = re.compile(r"^; ===== (\S+) @ 0x([0-9a-fA-F]+) \((\d+) bytes\)")
INS = re.compile(r"^([0-9a-f]{8})  (\S+)\s*(.*)$")
TGT = re.compile(r"(?:loc_|short loc_)([0-9A-F]+)")
JUMPS = {"jmp", "retn", "ret", "iret", "hlt"}


def blocks(path=ASM):
    out, fn = [], None
    def flush(f):
        if not f or not f["ins"]:
            return
        addrs = [a for a, _, _ in f["ins"]]
        aset = set(addrs)
        lead = {f["va"]}
        for i, (a, m, ops) in enumerate(f["ins"]):
            if m.startswith("j") or m in JUMPS or m.startswith("loop"):
                t = TGT.search(ops)
                if t and int(t.group(1), 16) in aset:
                    lead.add(int(t.group(1), 16))
                if i + 1 < len(addrs):
                    lead.add(addrs[i + 1])
        ls = sorted(x for x in lead if x in aset)
        end = f["va"] + f["size"]
        for k, a in enumerate(ls):
            out.append((a, (ls[k + 1] if k + 1 < len(ls) else end) - a, f["va"]))
    for line in open(path, errors="replace"):
        h = HDR.match(line)
        if h:
            flush(fn)
            fn = {"name": h.group(1), "va": int(h.group(2), 16), "size": int(h.group(3)), "ins": []}
            continue
        m = INS.match(line)
        if m and fn is not None:
            fn["ins"].append((int(m.group(1), 16), m.group(2), m.group(3)))
    flush(fn)
    return out


if __name__ == "__main__":
    b = blocks()
    print(len(b), "blocks", sum(s for _, s, _ in b), "bytes", len({f for *_, f in b}), "functions")
