#!/usr/bin/env python3
"""prove_no_retail_code.py — the standalone runs no retail code and ships none (OpenSummoners origin).

`rhost --standalone` maps only the NON-CODE sections of the user's own Lords2.exe at their retail VAs; the code range
(.text 0x401000..0x4d0000, incl. the GOG patch's code past .text's VirtualSize) holds INT3 plus a 5-byte `jmp` into
port code at every hook-table row's entry (tools/rhost/rh_hooks*.c).  This tool re-derives every claim independently
of the C side, from the files:

  1. image   — the copy rule (headers + every section without CNT_CODE / MEM_EXECUTE) applied to Lords2.exe takes no
               code: no IDA function lies in a copied section, no copied section is executable, and the copied bytes
               do not look like code (`55 8B EC` prologue density vs .text's)
  2. binary  — our exes embed no retail code: every byte span they share with retail .text (seeded by a 32-byte
               window, low-entropy ones aside, extended both ways) is short (< 128 B) and covers no whole retail
               function; the longest spans are listed with the symbol of ours they sit in
  3. source  — src/*.c|h and tools/rhost/*.c|h: every literal VA in the code range used as code (engine_retail_call /
               rcallN / nw_call / county_panel seams, function-pointer casts, by-address values: DLGPROC, WNDPROC,
               callback fields) is an IDA function entry; each is THUNKED (a hook row at that VA) or a GAP (no row:
               the standalone stops at that INT3 if it is reached — informational, the corpus decides reachability).
               Indirect seams through retail data tables (engine_retail_call(g_rl_<va>[k])) are expanded from the
               exe.  FAIL: a literal read of code-range bytes as data (G8/G16/G32/V32/AT/deref of a code-range VA)
  4. audit   — a run's standalone_audit.txt: verdict OK, only non-code copied, every non-thunk code byte INT3 (other
               0), no retail .text window present (identical 0), nothing executable outside the code range; every
               thunk an IDA function entry; the thunk set vs the hook rows in the tree (informational)

  tools/dev python3 tools/standalone/prove_no_retail_code.py [--audit RUN/boot/standalone_audit.txt] [--gaps-only]
Exit 0 = every check PASS.  stdlib only."""
import argparse
import bisect
import glob
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = "/mnt/ephemeral/gog/Lords of the Realm II/Lords2.exe"
IDA_TSV = "/mnt/ephemeral/openlords2/decompiled-ida/functions_ida.tsv"
SCN_CODE, SCN_EXEC = 0x20, 0x20000000
WIN = 32
MAXRUN = 128   # an identical span this long (or covering a whole retail function) is not convergent codegen

results = []


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"\n      {detail}" if detail and not ok else ""))


def info(msg):
    print(f"info  {msg}")


def pe(path):
    b = open(path, "rb").read()
    o = struct.unpack_from("<I", b, 0x3c)[0]
    nsec = struct.unpack_from("<H", b, o + 6)[0]
    optsz = struct.unpack_from("<H", b, o + 20)[0]
    ib = struct.unpack_from("<I", b, o + 52)[0]
    secs = []
    for i in range(nsec):
        s = o + 24 + optsz + 40 * i
        name = b[s:s + 8].rstrip(b"\0").decode("latin1")
        vs, va, rs, rp = struct.unpack_from("<IIII", b, s + 8)
        ch = struct.unpack_from("<I", b, s + 36)[0]
        # raw = every byte the file holds for the section (the GOG patch's code sits past .text's VirtualSize)
        secs.append(dict(name=name, va=ib + va, vs=vs or rs, rs=rs, rp=rp, raw=b[rp:rp + rs], ch=ch))
    return dict(path=path, data=b, ib=ib, secs=secs)


def is_code(s):
    return bool(s["ch"] & (SCN_CODE | SCN_EXEC))


_FUNCS = None


def ida_functions():
    """[(va, size, name, flirt_lib)] sorted by va"""
    global _FUNCS
    if _FUNCS is None:
        out = []
        for line in open(IDA_TSV):
            f = line.rstrip("\n").split("\t")
            if re.match(r"^[0-9a-f]{8}$", f[0]):
                out.append((int(f[0], 16), int(f[1]), f[2], f[3] == "1"))
        _FUNCS = sorted(out)
    return _FUNCS


def ida_names():
    return {va: n for va, _, n, _ in ida_functions()}


def low_entropy(w):
    return len(set(w)) <= 3


def code_range(exe):
    text = [s for s in exe["secs"] if is_code(s)]
    lo = min(s["va"] for s in text)
    hi = max(s["va"] + max(s["vs"], s["rs"]) for s in text)
    return lo, (hi + 0xfff) & ~0xfff


# ------------------------------------------------------------------ 1. image
def check_image(exe):
    funcs = ida_functions()
    text = [s for s in exe["secs"] if is_code(s)]
    copied = [s for s in exe["secs"] if not is_code(s)]
    info(f"{os.path.basename(exe['path'])}: copied {', '.join(s['name'] for s in copied)} (+ headers); "
         f"never copied: {', '.join(s['name'] for s in text)}")
    for s in text:
        if s["rs"] > s["vs"]:
            tail = s["raw"][s["vs"]:]
            info(f"{s['name']}: {s['rs'] - s['vs']} raw bytes past VirtualSize ({sum(1 for c in tail if c)} nonzero: "
                 f"the GOG patch's code) — inside the never-copied code range")
    hits = [(va, n, s["name"]) for va, sz, n, _ in funcs for s in copied
            if va < s["va"] + max(s["vs"], s["rs"]) and va + sz > s["va"]]
    check(f"image: no IDA function (of {len(funcs)}) lies in a copied section of Lords2.exe", not hits,
          "; ".join(f"{n}@{va:08x} in {sec}" for va, n, sec in hits[:10]))
    lo, hi = code_range(exe)
    outside = [(va, n) for va, sz, n, _ in funcs if va < lo or va + sz > hi]
    check(f"image: every IDA function lies in the code range {lo:08x}..{hi:08x}", not outside,
          "; ".join(f"{n}@{va:08x}" for va, n in outside[:10]))
    check("image: every copied section of Lords2.exe is non-executable data",
          all(not is_code(s) for s in copied))
    def dens(raw):
        return raw.count(b"\x55\x8b\xec") / max(1, len(raw)) * (1 << 20)
    td = sum(s["raw"].count(b"\x55\x8b\xec") for s in text) / max(1, sum(len(s["raw"]) for s in text)) * (1 << 20)
    cd = [(s["name"], dens(s["raw"])) for s in copied]
    info("prologue 55 8B EC per MB: .text %.0f; " % td + ", ".join(f"{n} {d:.1f}" for n, d in cd))
    check("image: copied bytes do not look like code (prologue density of every copied section < 5% of .text's)",
          all(d < 0.05 * td for _, d in cd))
    return text


# ------------------------------------------------------------------ 2. binary
def symbols(path):
    """our exe's text symbols, va-sorted: [(va, name)] (i686-w64-mingw32-nm)"""
    try:
        out = subprocess.run(["i686-w64-mingw32-nm", "-n", path], capture_output=True, text=True).stdout
    except OSError:
        return []
    syms = []
    for line in out.splitlines():
        f = line.split()
        if len(f) == 3 and f[1] in "tT":
            syms.append((int(f[0], 16), f[2]))
    return syms


def _abs_insn(b, i):
    """length of an x86 instruction at b[i] whose only operands are an absolute address (disp32) and an immediate —
    the encodings retail-layout C (globals at their retail VAs) compiles to byte for byte under any compiler:
    mov [abs],imm32 / imm16 / imm8, mov eax,[abs] / mov [abs],eax, mov r32,[abs] / mov [abs],r32,
    mov eax,[abs+eax*4]; 0 = not one"""
    n = len(b) - i
    c = b[i]
    if c == 0xC7 and n >= 10 and b[i + 1] == 0x05: return 10
    if c == 0xC6 and n >= 7 and b[i + 1] == 0x05: return 7
    if c == 0x66 and n >= 9 and b[i + 1] == 0xC7 and b[i + 2] == 0x05: return 9
    if c in (0xA1, 0xA3, 0xA0, 0xA2) and n >= 5: return 5
    if c in (0x8B, 0x89, 0x8A, 0x88) and n >= 6 and (b[i + 1] & 0xC7) == 0x05: return 6
    if c == 0x8B and n >= 7 and (b[i + 1] & 0xC7) == 0x04 and (b[i + 2] & 0x07) == 0x05: return 7
    return 0


def abs_run_coverage(b):
    """fraction of b covered by _abs_insn instructions (best of every decode start in the first 10 bytes: a span
    can begin mid-instruction)"""
    best = 0
    for s0 in range(min(10, len(b))):
        i, cov = s0, 0
        while i < len(b):
            k = _abs_insn(b, i)
            if k:
                cov += k
                i += k
            else:
                i += 1
        best = max(best, cov)
    return best / max(1, len(b))


def check_binaries(text, exes):
    tb = text[0]["raw"]
    t0 = text[0]["va"]
    idx = {}
    for o in range(len(tb) - WIN + 1):
        w = tb[o:o + WIN]
        if not low_entropy(w):
            idx.setdefault(w, o)
    funcs = ida_functions()
    fstarts = [f[0] for f in funcs]
    info(f"retail .text: {len(idx)} distinct {WIN}-byte windows (low-entropy ones aside)")

    def retail_func(va):
        i = bisect.bisect_right(fstarts, va) - 1
        return funcs[i] if i >= 0 and va < funcs[i][0] + funcs[i][1] else None

    for path in exes:
        if not os.path.exists(path):
            info(f"{path}: not built — skipped")
            continue
        img = pe(path)
        b = img["data"]
        spans, o = [], 0
        while o < len(b) - WIN + 1:
            t = idx.get(b[o:o + WIN])
            if t is None:
                o += 1
                continue
            n = WIN
            while o + n < len(b) and t + n < len(tb) and b[o + n] == tb[t + n]:
                n += 1
            k = 0
            while o - k > 0 and t - k > 0 and b[o - k - 1] == tb[t - k - 1]:
                k += 1
            spans.append((o - k, t0 + t - k, n + k))
            o += n

        def va_of(off):
            for s in img["secs"]:
                if s["rp"] <= off < s["rp"] + s["rs"]:
                    return s["va"] + off - s["rp"], s["name"]
            return None, "(hdr)"
        syms = symbols(path)
        svas = [a_ for a_, _ in syms]

        def owner(off):
            v, sec = va_of(off)
            if v is None:
                return sec
            if sec != ".text" or not syms:
                return f"{sec}+{v - img['ib']:x}"
            i = bisect.bisect_right(svas, v) - 1
            return f"{syms[i][1]}+0x{v - syms[i][0]:x}" if i >= 0 else "?"

        whole = [(o_, va, n, f) for o_, va, n in spans for f in funcs
                 if f[1] >= 16 and va <= f[0] and f[0] + f[1] <= va + n]
        cov = {sp: abs_run_coverage(b[sp[0]:sp[0] + sp[2]]) for sp in spans}
        absrun = [sp for sp in spans if sp[2] >= MAXRUN and cov[sp] >= 0.85]
        long_ = [sp for sp in spans if sp[2] >= MAXRUN and sp not in absrun]
        nolib = [sp for sp in spans if not any(sp[1] <= f[0] + f[1] and f[0] <= sp[1] + sp[2] and f[3] for f in funcs)]
        if absrun:
            info(f"{len(absrun)} spans ≥{MAXRUN} B are runs of absolute-operand moves (≥85% `mov [abs32],imm` / "
                 "`mov reg,[abs32]` / `mov [abs32],reg`): retail-layout globals initialised in retail's order compile "
                 "to the same bytes — convergent, not copied: " +
                 "; ".join(f"retail {va:08x} +{n} ({cov[(o_, va, n)]:.0%})" for o_, va, n in absrun))
        check(f"binary: {os.path.relpath(path, ROOT)} ({len(b) >> 10} KB): no span shared with retail .text is "
              f"≥{MAXRUN} B (absolute-move runs aside) or a whole function ({len(spans)} spans, longest "
              f"{max((n for _, _, n in spans), default=0)} B, longest other than an absolute-move run "
              f"{max((n for sp in spans for n in [sp[2]] if sp not in absrun), default=0)} B)",
              not long_ and not whole,
              "; ".join(f"off {o_:x} = retail {va:08x} +{n}" for o_, va, n in long_[:6]) +
              "; ".join(f"off {o_:x} covers {f[2]}@{f[0]:08x}" for o_, va, n, f in whole[:6]))
        for o_, va, n in sorted(spans, key=lambda s: -s[2])[:10]:
            f = retail_func(va)
            fn = f"{f[2]}@{f[0]:08x}{' (lib)' if f[3] else ''}" if f else "outside every IDA function"
            info(f"  span {n:3d} B  abs-moves {cov[(o_, va, n)]:4.0%}  ours {owner(o_)}  = retail {va:08x} in {fn}")
        if spans:
            info(f"  {len(spans)} spans, {len(spans) - len(nolib)} over IDA library (FLIRT) functions, "
                 f"{sum(n for _, _, n in spans)} B total")


# ------------------------------------------------------------------ 3. source
ROW_RE = re.compile(r'\{\s*"(\w+)"\s*,\s*0x([0-9a-fA-F]+)u?\s*,\s*\(void\s*\*\)\s*\w+')
LIT_RE = re.compile(r'\b0x(?:00)?(4[0-9a-fA-F]{5})u?\b')
CALL_CTX = re.compile(r'(?:\bengine_retail_call|\brcall\d|\bnw_call|\bcounty_panel|\bRETAIL)\s*\(\s*\(*\s*(?:\([^()]*\)\s*)?$')
READ_CTX = re.compile(r'(?:\b(?:G8|G16|G32|V8|V16|V32|D|AT)\s*\((?:[^()]*,\s*)*|\*\s*\(\s*(?:const\s+)?(?:volatile\s+)?'
                      r'(?:u?int(?:8|16|32)_t|char|unsigned\s+\w+|short|int|float|double)\s*\*\s*\)\s*\(\s*uintptr_t\s*\)\s*)$')


# code-range literals that are no use as code, checked by hand (VA -> why)
NOT_CODE_USES = {
    0x4c3370: "L2_ENTRY (rhost.h): rhost.c only compares the PE's entry point with it; --standalone enters sa_entry",
}


def strip_c(s, strings=True):
    """comments (and string literals) blanked, line numbers kept"""
    s = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group()), s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', '""', s) if strings else s


def source_files():
    out = []
    for pat in ("src/*.c", "src/*.h", "tools/rhost/*.c", "tools/rhost/*.h"):
        out += sorted(glob.glob(os.path.join(ROOT, pat)))
    return out


def hook_rows():
    """every hook-table row in the tree: [(va, name, file:line)] — sa_install thunks each row's VA"""
    rows = []
    for p in sorted(glob.glob(os.path.join(ROOT, "tools", "rhost", "rh_hooks*.c"))):
        s = strip_c(open(p, errors="replace").read(), strings=False)
        for m in ROW_RE.finditer(s):
            rows.append((int(m.group(2), 16), m.group(1), f"{os.path.relpath(p, ROOT)}:{s.count(chr(10), 0, m.start()) + 1}"))
    return rows


def check_source(exe, gaps_only=False):
    lo, hi = code_range(exe)
    names = ida_names()
    rows = hook_rows()
    thunked = {va for va, _, _ in rows}
    byva = {}
    for va, n, w in rows:
        byva.setdefault(va, []).append(n)
    dup = {va: ns for va, ns in byva.items() if len(ns) > 1}
    info(f"{len(rows)} hook rows, {len(thunked)} distinct entries (= the standalone's thunks); "
         f"{len(dup)} entries with two rows (the first table's wrapper wins, rh_hooks.c sa_thunk): " +
         " ".join(f"{va:x}={'/'.join(ns)}" for va, ns in sorted(dup.items())))
    badrow = [(va, n, w) for va, n, w in rows if va not in names]
    check(f"source: every hook row's VA is an IDA function entry ({len(rows)} rows)", not badrow,
          "; ".join(f"{n}@{va:x} ({w})" for va, n, w in badrow[:10]))

    uses = {}          # va -> [(kind, where)]
    reads, odd, cmps, labels = [], [], 0, 0
    tables = {}        # retail table VA -> where (engine_retail_call(g_rl_<va>[…]))
    row_lines = {w for _, _, w in rows}
    data = {s["va"]: s for s in exe["secs"] if not is_code(s)}
    decl = {}          # g_*_<va>[N] declarations: the table bound
    hybrid_only = []   # literals only the hybrid reaches (RL_PLAT_CRT: the standalone binds the host CRT)
    for p in source_files():
        rel = os.path.relpath(p, ROOT)
        s = strip_c(open(p, errors="replace").read())
        for m in re.finditer(r"engine_retail_call\s*\(\s*\(\s*uint32_t\s*\)\s*g_\w*?_([0-9a-f]{6})\s*\[", s):
            tables.setdefault(int(m.group(1), 16), f"{rel}:{s.count(chr(10), 0, m.start()) + 1}")
        for m in re.finditer(r"\bg_\w*?_([0-9a-f]{6})\s*\[\s*(\d+)\s*\]", s):
            decl.setdefault(int(m.group(1), 16), int(m.group(2)))
        # multi-line #define bodies: which macro a line belongs to
        macro_of, cur = {}, None
        for i, ln in enumerate(s.split("\n"), 1):
            mm = re.match(r"\s*#\s*define\s+(\w+)", ln)
            if mm:
                cur = mm.group(1)
            if cur:
                macro_of[i] = cur
            if not ln.rstrip().endswith("\\"):
                cur = None
        lits = [(int(m.group(1), 16), m) for m in LIT_RE.finditer(s)]
        if p.endswith("_globals.c"):   # retail tables transcribed in decimal (g_rl_4d57f0[100] = {4460624, …})
            lits += [(int(m.group(1)), m) for m in re.finditer(r"(?<![\w.])(4[0-9]{6}|50[0-4][0-9]{4})(?![\w.])", s)]
        for va, m in lits:
            if not (lo <= va < hi):
                continue
            line_no = s.count("\n", 0, m.start()) + 1
            where = f"{rel}:{line_no}"
            if where in row_lines:
                continue
            if va in NOT_CODE_USES:
                continue
            if macro_of.get(line_no) == "RL_PLAT_CRT":
                hybrid_only.append((va, where))
                continue
            ls = s.rfind("\n", 0, m.start()) + 1
            pre, post = s[ls:m.start()], s[m.end():s.find("\n", m.end())]
            if "CALL_TRACE_ENTER" in pre:
                labels += 1
                continue
            if READ_CTX.search(pre):
                reads.append(f"{where} 0x{va:x}{' (entry ' + names[va] + ')' if va in names else ''}")
                continue
            if re.search(r"(?:\bcase|[!=]=)\s*$", pre) or re.match(r"\s*[!=]=", post):
                cmps += 1
                continue
            if va not in names:
                odd.append(f"{where} 0x{va:x}: {pre.strip()[-50:]}0x{m.group(1)}")
                continue
            called = bool(CALL_CTX.search(pre)) or bool(re.match(r"\s*u?\s*\)\s*\(", post))
            uses.setdefault(va, []).append(("call" if called else "addr", where))
    # indirect seams through retail data tables: expand the table from the exe (entries while each is an IDA entry)
    tab_entries = {}
    for tva, where in sorted(tables.items()):
        sec = next((s for s in data.values() if s["va"] <= tva < s["va"] + s["vs"]), None)
        if not sec:
            continue
        ents, o = [], tva - sec["va"]
        while o + 4 <= len(sec["raw"]) and len(ents) < decl.get(tva, 64):
            v = struct.unpack_from("<I", sec["raw"], o)[0]
            if v not in names:
                break
            ents.append(v)
            o += 4
        tab_entries[tva] = (where, ents)
        for v in ents:
            uses.setdefault(v, []).append(("table", f"{where} (g_*_{tva:x}[{ents.index(v)}])"))

    if not gaps_only:
        check("source: no port code reads code-range bytes as data (G8/G16/G32/V32/D/AT/deref of a code-range VA)",
              not reads, "; ".join(reads[:12]))
        info(f"code-range literals: {len(uses)} distinct entries used as code ({sum(len(v) for v in uses.values())} "
             f"sites), {cmps} identity compares (case / ==), {labels} CALL_TRACE labels skipped")
        if odd:
            info("code-range literals that are no function entry (values, not reads — verify by hand): " + "; ".join(odd))
        for tva, (where, ents) in tab_entries.items():
            info(f"retail table {tva:08x} ({where}; bound {decl.get(tva, 'none: entries while IDA entries')}): "
                 f"{len(ents)} function pointers, "
                 f"{sum(1 for v in ents if v in thunked)} thunked")
        if hybrid_only:
            info(f"{len(hybrid_only)} retail CRT entries (RL_PLAT_CRT, src/rl_io_plat.h) are hybrid-only: --standalone binds "
                 "the host CRT (rl_plat_bind_imports: s_crt 0), never CRT(n): " +
                 " ".join(f"{va:x}={names.get(va, '?')}" for va, _ in hybrid_only))
        info("not counted (no use as code): " + "; ".join(f"{va:x} {why}" for va, why in NOT_CODE_USES.items()))
        info("indirect seams not expandable statically (runtime pointers: the corpus decides): "
             "engine_retail_call(*(r + 8)) rl_ui4.c, (g_rl_522ca8 + 4) rl_glue.c, g_rl_552ff4 rl_ctl.c")
    gaps = sorted(va for va in uses if va not in thunked)
    th = len(uses) - len(gaps)
    print(f"info  source: {len(uses)} retail entries referenced as code — {th} thunked, {len(gaps)} GAPS "
          "(no hook row: the standalone stops there if reached):")
    for va in gaps:
        sites = list(dict.fromkeys(uses[va]))
        kinds = "/".join(sorted({k for k, _ in sites}))
        print(f"  gap {va:08x} {names.get(va, '?'):34s} {kinds:10s} " +
              ", ".join(w for _, w in sites[:4]) + (f" (+{len(sites) - 4})" if len(sites) > 4 else ""))
    return thunked


# ------------------------------------------------------------------ 4. audit
def check_audit(path, exe, thunked):
    lines = open(path, errors="replace").read().splitlines()
    kv, ranges, thunks, fails = {}, [], [], []
    for l in lines:
        f = l.split()
        if not f or l.startswith("#"):
            continue
        if f[0] == "range":
            a, b_ = f[2].split("..")
            ranges.append((f[1], int(a, 16), int(b_, 16), int(f[4], 16), f[5]))
        elif f[0] == "thunk":
            thunks.append((int(f[1], 16), f[4] if len(f) > 4 else "?"))
        elif f[0] == "FAIL":
            fails.append(l)
        elif f[0] != "protect":
            kv[f[0]] = f[1:]
    check("audit: verdict OK, no FAIL line", kv.get("verdict") == ["OK"] and not fails,
          " ".join(kv.get("verdict", ["missing"])) + " " + "; ".join(fails[:6]))
    badr = [r for r in ranges if (r[3] & (SCN_CODE | SCN_EXEC)) and r[4] == "copied"]
    notc = [r for r in ranges if r[4] != "copied"]
    want = [s["name"] for s in exe["secs"] if is_code(s)]
    check(f"audit: {len(ranges)} ranges, every copied one non-code; not copied: "
          f"{', '.join(r[0] for r in notc)} (= the exe's code sections)",
          not badr and sorted(r[0] for r in notc) == sorted(want), "; ".join(f"{r[0]} copied" for r in badr))
    cr = kv.get("code_range", [])
    def field(k):
        return int(cr[cr.index(k) + 1]) if k in cr else -1
    other, int3, nb = field("other"), field("int3"), field("bytes")
    lo, hi = code_range(exe)
    crange = cr[0] if cr else "?"
    check(f"audit: code range {crange} (exe: {lo:08x}..{hi:08x}) is INT3 but for {len(thunks)} thunks "
          f"(int3 {int3} + 5×thunks = {int3 + 5 * len(thunks)} of {nb} bytes; other {other})",
          other == 0 and crange == f"{lo:08x}..{hi:08x}" and int3 + 5 * len(thunks) == nb)
    rw = kv.get("retail_text_windows_compared", [])
    same = int(rw[rw.index("identical") + 1]) if "identical" in rw else -1
    check(f"audit: no 16-byte window of retail .text in memory ({rw[0] if rw else '?'} compared, identical {same})",
          same == 0)
    check("audit: nothing executable outside the code range", kv.get("exec_outside_code_range") == ["0"])
    names = ida_names()
    noent = [(va, n) for va, n in thunks if va not in names]
    check(f"audit: every one of {len(thunks)} thunks is at an IDA function entry", not noent,
          " ".join(f"{n}@{va:x}" for va, n in noent[:8]))
    tv = {va for va, _ in thunks}
    extra, missing = sorted(tv - thunked), sorted(thunked - tv)
    info(f"audit thunks vs the tree's hook rows: {len(tv & thunked)} common, {len(extra)} only in the run, "
         f"{len(missing)} only in the tree" + (f" (run: {[hex(v) for v in extra[:6]]} tree: {[hex(v) for v in missing[:6]]}"
                                               " — the build is older/newer than the tree)" if extra or missing else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=EXE)
    ap.add_argument("--audit", help="a run's standalone_audit.txt")
    ap.add_argument("--binaries", nargs="*", default=[os.path.join(ROOT, "build", "rhost", "rhost.exe"),
                                                      os.path.join(ROOT, "build", "openlords2.exe")])
    ap.add_argument("--no-binary", action="store_true", help="skip the binary window scan")
    ap.add_argument("--gaps-only", action="store_true", help="only the source check's gap list")
    a = ap.parse_args()
    exe = pe(a.exe)
    if a.gaps_only:
        check_source(exe, gaps_only=True)
        return 0
    text = check_image(exe)
    if not a.no_binary:
        check_binaries(text, a.binaries)
    thunked = check_source(exe)
    if a.audit:
        check_audit(a.audit, exe, thunked)
    nfail = sum(1 for _, ok in results if not ok)
    print(f"\n{'PROVEN' if not nfail else 'NOT PROVEN'}: {len(results) - nfail}/{len(results)} checks pass")
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
