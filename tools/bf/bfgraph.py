"""tools/bf/bfgraph.py — the battlefield port's analysis library (docs/findings/battlefield-sim-port.md).

Reads the user's Lords2.exe (PE sections, dispatch tables), the IDA export (function table, data xrefs), the
derived port ledger, the latest rhost coverage run and the rhost hook tables. Used by gen_skeleton.py and
status.py; nothing here writes files.
"""
import collections
import csv
import glob
import json
import os
import re
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EPH = os.environ.get("OL2_EPHEMERAL", "/mnt/ephemeral/openlords2")
IDA = os.path.join(EPH, "decompiled-ida")
EXE = os.path.join(ROOT, "vendor", "original", "Lords2.exe")
if not os.path.exists(EXE):   # a worktree has no vendor/ symlink: the main tree's
    EXE = "/opt/src/OpenLords2/vendor/original/Lords2.exe"

# ---------------------------------------------------------------- the exe
_data = open(EXE, "rb").read()
_pe = struct.unpack_from("<I", _data, 0x3c)[0]
_nsec = struct.unpack_from("<H", _data, _pe + 6)[0]
_opt = struct.unpack_from("<H", _data, _pe + 20)[0]
SECTIONS = []   # (name, va, vsize, raw_off, raw_size)
for _i in range(_nsec):
    _o = _pe + 24 + _opt + 40 * _i
    _vs, _va, _rs, _ra = struct.unpack_from("<IIII", _data, _o + 8)
    SECTIONS.append((_data[_o:_o + 8].rstrip(b"\0").decode(), 0x400000 + _va, _vs, _ra, _rs))


def section_of(va):
    for s in SECTIONS:
        if s[1] <= va < s[1] + max(s[2], s[4]):
            return s
    return None


def rd(va, n):
    """n bytes of the exe image at va (zero past the file-backed part: .bss)"""
    s = section_of(va)
    if not s:
        raise KeyError(hex(va))
    off = va - s[1]
    b = _data[s[3] + off:s[3] + min(off + n, s[4])] if off < s[4] else b""
    return b + b"\0" * (n - len(b))


def u32(va):
    return struct.unpack("<I", rd(va, 4))[0]


# ---------------------------------------------------------------- the IDA function table
F = {}
for r in csv.DictReader(open(os.path.join(IDA, "functions_ida.tsv")), delimiter="\t"):
    va = int(r["va"], 16)
    r["callees_l"] = [int(x, 16) for x in r["callees"].split(",") if x]
    r["callers_l"] = [int(x, 16) for x in r["callers"].split(",") if x]
    r["size_i"] = int(r["size"])
    F[va] = r

# the battlefield's dispatch tables (function-pointer arrays in .data) and who indexes them
DISPATCH = {
    0x4d9140: (11, "unit movement state (FUN_004822ed: unit +0x12, 0..10)"),
    0x4d9170: (0x12, "unit order (the state handlers: unit +0x31, 0..0x11)"),
    0x4d91b8: (5, "group stance, interactive (FUN_00489401)"),
    0x4d91f8: (0xb, "group stance, auto (FUN_00489401)"),
    0x4d91d0: (9, "group stance, third table (FUN_00489401)"),
}
TABLE_ENTRIES = {t: [u32(t + 4 * i) for i in range(n)] for t, (n, _) in DISPATCH.items()}
_extra = collections.defaultdict(list)
for _v in TABLE_ENTRIES[0x4d9140]:
    _extra[_v] += TABLE_ENTRIES[0x4d9170]   # every movement-state handler dispatches the order table
_extra[0x4822ed] += TABLE_ENTRIES[0x4d9140]
for _t in (0x4d91b8, 0x4d91f8, 0x4d91d0):
    _extra[0x489401] += TABLE_ENTRIES[_t]


def callees(v):
    return [c for c in F[v]["callees_l"] + _extra.get(v, []) if c in F]


def is_lib(v):
    return F[v]["flirt_lib"] == "1"


# ---------------------------------------------------------------- port status
LEDGER = {int(f["va"], 16): f for f in json.load(open(os.path.join(ROOT, "docs", "port-ledger.json")))["functions"]}


def status(v):
    f = LEDGER.get(v)
    return f["status"] if f else "?"


def ported(v):
    return status(v) in ("tested", "ported")


def coverage():
    """{va: scenario count} from the newest rhost coverage run (rhost.py coverage)"""
    runs = sorted(glob.glob(os.path.join(EPH, "rhost-wine", "root", "runs", "*-coverage", "coverage_summary.tsv")) +
                  glob.glob(os.path.join(EPH, "rhost-runs", "*-coverage", "coverage_summary.tsv")), key=os.path.getmtime)
    if not runs:
        return {}
    return {int(r["va"], 16): len(r["scenarios"].split(",")) for r in csv.DictReader(open(runs[-1]), delimiter="\t")}


def hooked():
    """{va: row name} over every rhost hook table"""
    out = {}
    for fn in glob.glob(os.path.join(ROOT, "tools", "rhost", "rh_hooks*.c")):
        for m in re.finditer(r'\{ "(\w+)", 0x([0-9a-f]+)u', open(fn).read()):
            out[int(m.group(2), 16)] = m.group(1)
    return out


def data_xrefs():
    """[(target_va, target_name, func_va, kind)]"""
    out = []
    for r in csv.DictReader(open(os.path.join(IDA, "data_xrefs.tsv")), delimiter="\t"):
        out.append((int(r["target_va"], 16), r["target_name"], int(r["func_va"], 16), r["kind"]))
    return out


# ---------------------------------------------------------------- port symbols already bound to retail VAs
SRC = os.path.join(ROOT, "src")
_TY = {"uint8_t": 1, "int8_t": 1, "char": 1, "uint16_t": 2, "int16_t": 2, "uint32_t": 4, "int32_t": 4, "int": 4,
       "unsigned": 4, "long": 4}


def _symsize(name, texts):
    pat = re.compile(r"\b(u?int(?:8|16|32)_t|int|char|unsigned|long)\s+\**%s\s*((?:\[[^\]]+\])*)" % re.escape(name))
    for t in texts:
        for m in pat.finditer(t):
            sz = _TY[m.group(1)]
            for d in re.findall(r"\[([^\]]+)\]", m.group(2)):
                try:
                    sz *= int(eval(d, {"CASTLES_BLOCK_BYTES": 12800}))
                except Exception:
                    return None
            return sz
    return None


def port_owners(include_bf=False, exclude=()):
    """[(lo, hi, symbol, kind)] — the save block (save_schema_table.h) and every runtime global the rhost tables
    alias to a retail VA (asm `A(name,0xVA)` lines; size from the port's declaration, else 4)"""
    out = []
    for l in open(os.path.join(SRC, "save_schema_table.h")):
        m = re.search(r"\{\s*&?\s*(\w+)\s*,\s*(\d+)\s*\}.*retail 0x([0-9a-f]+)", l)
        if m:
            lo = int(m.group(3), 16)
            out.append((lo, lo + int(m.group(2)), m.group(1), "save"))
    texts = [open(f, errors="replace").read() for f in glob.glob(os.path.join(SRC, "*.[ch]"))]
    for fn in glob.glob(os.path.join(ROOT, "tools", "rhost", "rh_*.[ch]")):
        if not include_bf and os.path.basename(fn).startswith("rh_bf_alias"):
            continue
        if os.path.basename(fn) in exclude:
            continue
        for n, v in re.findall(r"A\((\w+),\s*(0x[0-9a-fA-F]+)\)", open(fn).read()):
            v = int(v, 16)
            if any(lo <= v < hi for lo, hi, _, _ in out):
                continue
            out.append((v, v + (_symsize(n, texts) or 4), n, "alias"))
    return sorted(out)
