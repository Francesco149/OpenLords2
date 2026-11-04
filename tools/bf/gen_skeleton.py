#!/usr/bin/env python3
"""tools/bf/gen_skeleton.py — scaffold the battlefield port (docs/findings/battlefield-sim-port.md).

Writes, once (never over an existing partition file — the owners edit those):
  src/engine_bf_globals.{h,c}       every retail global the battlefield functions touch that has no port symbol yet,
                                    as g_bf_<va> (one block per retail variable / struct array; .data initializers)
  tools/rhost/rh_bf_alias.h          the asm alias block that puts every g_bf_<va> at its retail VA in rhost.exe
  src/engine_bf_<part>.h            the partition's functions, canonical names bf_<va>, retail arity
  src/engine_bf_<part>_pending.c    a retail stub per function (engine_retail_call): delete one as you port it
  src/engine_bf_<part>.c            the partition's ports (empty)
  tools/rhost/rh_hooks_bf_<part>.c  the partition's hook table (empty)
--globals-only regenerates only the first two (the partition files stay); --new-parts only the partitions that
have no files yet (the globals stay — once partitions use them, regenerate only with every owner's consent).
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import bfgraph as G
from parts import PARTS, OWNER, FAMILY

ROOT = G.ROOT
SRC = os.path.join(ROOT, "src")
RH = os.path.join(ROOT, "tools", "rhost")
WIDTH = {"byte": 1, "word": 2, "dword": 4, "off": 4, "unk": 1, "funcs": 4, "asc": 1, "stru": 1, "qword": 8}
CTYPE = {1: "uint8_t", 2: "int16_t", 4: "int32_t"}
EXTRA = [0x5aea58, 0x58fd7c, 0x58fd70, 0x5aea5c, 0x58fd60, 0x58fd6c]


def N(p):
    """the names a partition's files and symbols use: family 'bf' (the battlefield: engine_bf_<p>, bf_<va>) or 'rl'
    (any other retail-layout port: rl_<p>, rl_<va>)"""
    fam = FAMILY.get(p, "bf")
    if fam == "bf":
        return dict(fam="bf", fn="bf", stem=f"engine_bf_{p}", hdr="engine_bf.h", table=f"RH_HOOKS_BF_{p.upper()}",
                    hooks=f"rh_hooks_bf_{p}.c", g="g_bf", alias=f"rh_bf_{p}_alias.h", what="the battlefield port")
    return dict(fam="rl", fn="rl", stem=f"rl_{p}", hdr="rl.h", table=f"RH_HOOKS_RL_{p.upper()}",
                hooks=f"rh_hooks_rl_{p}.c", g="g_rl", alias=f"rh_rl_{p}_alias.h", what="a retail-layout port")


def width_of(name):
    return WIDTH.get(name.split("_")[0], 1)


def globals_blocks(funcs=None, include_bf=False, exclude=()):
    funcs = set(OWNER) if funcs is None else set(funcs)
    owners = G.port_owners(include_bf=include_bf, exclude=exclude)

    def owned(v):
        for lo, hi, n, k in owners:
            if lo <= v < hi:
                return n
        return None
    xr = G.data_xrefs()
    allref = collections.defaultdict(set)
    names = {}
    bf = collections.defaultdict(set)
    for t, tn, f, kind in xr:
        allref[t].add(f)
        names[t] = tn
        if f in funcs:
            bf[t].add(kind)
    # the dispatch tables are port tables of port functions, never data
    # + what engine_bf_common.c's helpers write (bf_rng_step: FUN_00404a46's six outputs)
    for v in (EXTRA if not include_bf else []):
        bf[v].add("w")
        if v not in names:
            names[v] = f"dword_{v:X}"
    vas = sorted(v for v in bf if not owned(v) and v not in G.DISPATCH and G.section_of(v) and v >= 0x4d0000)
    R = sorted(allref)
    import bisect
    import re
    # a global IDA never indexes nor takes the address of (anywhere in the exe) is a scalar: its own symbol; the
    # rest (arrays, a struct array's fields) cluster into one block per run of nearby array members
    ida = open(os.path.join(G.IDA, "all_ida.c"), errors="replace").read()
    nonscalar = set(re.findall(r"\b((?:byte|word|dword|unk|off|stru|asc|qword)_[0-9A-F]+)\s*\[", ida)) | \
        set(re.findall(r"&((?:byte|word|dword|unk|off|stru|asc|qword)_[0-9A-F]+)\b", ida))

    def scalar(v):
        n = names[v]
        return n not in nonscalar and n.split("_")[0] in ("byte", "word", "dword")
    blocks = []   # [members]
    for v in vas:
        if blocks and not scalar(v) and not scalar(blocks[-1][0]):
            prev = blocks[-1][-1]
            between = [r for r in R[bisect.bisect_right(R, prev):bisect.bisect_left(R, v)]]
            if v - prev <= 0x100 and all(r in bf and not scalar(r) for r in between):
                blocks[-1].append(v)
                continue
        blocks.append([v])
    out = []
    for mem in blocks:
        lo, last = mem[0], mem[-1]
        s = G.section_of(lo)
        sec_end = s[1] + max(s[2], s[4])
        i = bisect.bisect_right(R, last)
        nxt = R[i] if i < len(R) else sec_end
        hi = min(nxt, sec_end, lo + 0x10000)
        if len(mem) == 1 and scalar(lo):
            hi = min(hi, lo + width_of(names[lo]))
        for olo, ohi, n, k in owners:   # never overlap a port symbol
            if lo < olo < hi:
                hi = olo
        out.append((lo, hi, mem, [names[m] for m in mem], s[0], {m: "".join(sorted(bf[m])) for m in mem},
                    any(not scalar(m) for m in mem)))
    return out, owners


def gen_globals(part=None):
    """part=None: engine_bf_globals.{h,c} + rh_bf_alias.h for every partition; part=NAME: the globals that partition
    needs beyond those (and beyond every port symbol): engine_bf_NAME_globals.{h,c} + rh_bf_NAME_alias.h"""
    nm = N(part) if part else N("x")
    blocks, owners = globals_blocks(PARTS[part] if part else None, include_bf=bool(part),
                                    exclude=(nm["alias"],) if part else ())   # its own aliases are not owners
    stem, macro = (f"{nm['stem']}_globals", f"RH_{nm['fam'].upper()}_{part.upper()}_ALIASES") if part else ("engine_bf_globals", "RH_BF_ALIASES")
    alias_h = nm["alias"] if part else "rh_bf_alias.h"
    gpre = nm["g"]
    src_txt = "".join(open(os.path.join(SRC, f), errors="replace").read().lower()
                      for f in os.listdir(SRC) if f.endswith((".c", ".h")) and not f.startswith("engine_bf_"))
    h = ["/* engine_bf_globals.h — GENERATED by tools/bf/gen_skeleton.py --globals-only: the retail globals the battlefield",
         " * port touches that had no port symbol, one g_bf_<va> per retail variable (a struct array's fields together: a",
         " * uint8_t block; access a field with AT(T, g_bf_<lo>, 0x<lo>, VA)). rhost aliases every one at its VA",
         " * (tools/rhost/rh_bf_alias.h); the standalone defines them in engine_bf_globals.c (.data initializers kept).",
         " * DUP = another src/ module names this VA already (file-static or unlinked): the standalone must reconcile",
         " * it with that module before the battlefield is wired in. Edit by hand only to add a global; widths are",
         " * IDA's (signedness is the port's call: cast). */",
         "#pragma once", "#include <stdint.h>", ""]
    c = ["/* engine_bf_globals.c — GENERATED (tools/bf/gen_skeleton.py): storage for engine_bf_globals.h in the standalone.",
         " * NOT linked into rhost (tools/rhost/rh_bf_alias.h puts each symbol at its retail VA). */",
         '#include "engine_bf_globals.h"', ""]
    al = [f"/* {alias_h} — GENERATED (tools/bf/gen_skeleton.py): every {stem}.h symbol at its retail VA. */",
          "#pragma once", "#define BFA(n, v) \".globl _\" #n \"\\n\\t.set _\" #n \", \" #v \"\\n\\t\"", f"#define {macro} \\"]
    for lo, hi, mem, nm, sec, kinds, indexed in blocks:
        size = hi - lo
        sym = f"{gpre}_{lo:06x}"
        dup = [f"{m:06x}" for m in mem if (f"{m:08x}" in src_txt or f"0x{m:x}" in src_txt)]
        note = f"{sec} {lo:#x}..{hi:#x}: " + " ".join(f"{n}({kinds[m]})" for m, n in zip(mem, nm))
        if dup:
            note += "  DUP " + " ".join(dup)
        below = [n for olo, ohi, n, k in owners if ohi - olo >= 0x1000 and 0 < olo - lo <= 0x300]
        if below and indexed:   # IDA's name for a grid's neighbour (the row / cell above): byte_543E61[off] is the cell grid at off - 0x27f
            note = f"NEIGHBOUR of {below[0]}: access it through {below[0]} (AT / BCELL8), never this block — " + note
        w = width_of(nm[0])
        if len(mem) == 1 and size == w and w in CTYPE:
            decl, n_el, ty = f"{CTYPE[w]} {sym}", 0, CTYPE[w]
        elif len(mem) == 1 and w in CTYPE and size % w == 0:
            decl, n_el, ty = f"{CTYPE[w]} {sym}[{size // w}]", size // w, CTYPE[w]
        else:
            decl, n_el, ty = f"uint8_t {sym}[{size:#x}]", size, "uint8_t"
        h.append(f"extern {decl};".ljust(44) + f" /* {note} */")
        init = G.rd(lo, size) if sec in (".data", ".rdata") and lo - G.section_of(lo)[1] < G.section_of(lo)[4] else b""
        if init and any(init):
            if n_el == 0:
                val = int.from_bytes(init, "little", signed=True)
                c.append(f"{decl} = {val};")
            else:
                ws = w if ty != "uint8_t" else 1
                vals = [int.from_bytes(init[i:i + ws], "little", signed=ws > 1) for i in range(0, size, ws)]
                while vals and vals[-1] == 0:
                    vals.pop()
                body = ",".join(str(x) for x in vals)
                c.append(f"{decl} = {{{body}}};")
        else:
            c.append(f"{decl};")
        al.append(f"    BFA({sym},{lo:#x}) \\")
    al.append("")
    if part:
        h[0] = f"/* {stem}.h — GENERATED by tools/bf/gen_skeleton.py --part-globals {part}: what partition '{part}' needs beyond"
        h[1] = " * engine_bf_globals.h, same conventions (see there). Include it from the partition's .c only."
        c[0] = f"/* {stem}.c — GENERATED (tools/bf/gen_skeleton.py): storage for {stem}.h in the standalone."
        c[2] = f'#include "{stem}.h"'
    open(os.path.join(SRC, stem + ".h"), "w").write("\n".join(h) + "\n")
    open(os.path.join(SRC, stem + ".c"), "w").write("\n".join(c) + "\n")
    open(os.path.join(RH, alias_h), "w").write("\n".join(al) + "\n")
    print(f"globals: {len(blocks)} blocks, {sum(b[1] - b[0] for b in blocks)} bytes")


def sig(v):
    n = int(G.F[v]["nargs"]) if G.F[v]["cc"] == "cdecl" else 0
    params = ", ".join(f"int a{i}" for i in range(n)) or "void"
    args = "".join(f", (uint32_t)a{i}" for i in range(n))
    return n, params, args


def gen_parts():
    cov = G.coverage()
    for p, vs in PARTS.items():
        nm = N(p)
        st, fn = nm["stem"], nm["fn"]
        hp = os.path.join(SRC, f"{st}.h")
        if os.path.exists(hp):
            print(f"{p}: exists, skipped")
            continue
        h = [f"/* {st}.h — {nm['what']}, partition '{p}' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).",
             f" * Canonical names {fn}_<va>; a function lives in {st}.c once ported, else in {st}_pending.c",
             " * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */",
             "#pragma once", ""]
        pend = [f"/* {st}_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost",
                " * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in",
                f" * {st}.c, add its row to tools/rhost/{nm['hooks']}. */",
                f'#include "{nm["hdr"]}"', ""]
        for v in vs:
            n, params, args = sig(v)
            f = G.F[v]
            h.append(f"int {fn}_{v:06x}({params});".ljust(48) + f" /* {f['size']} B, coverage {cov.get(v, 0)} */")
            pend.append(f"int {fn}_{v:06x}({params}) {{ return (int)engine_retail_call(0x{v:06x}u, {n}{args}); }}")
        open(hp, "w").write("\n".join(h) + "\n")
        open(os.path.join(SRC, f"{st}_pending.c"), "w").write("\n".join(pend) + "\n")
        open(os.path.join(SRC, f"{st}.c"), "w").write(
            f"/* {st}.c — {nm['what']}, partition '{p}' (tools/bf/parts.py). Retail semantics on retail's\n"
            f" * layout: {nm['hdr']} (record accessors, globals). Each function: \"Port of FUN_00xxxxxx\" + what it does. */\n"
            f'#include "{nm["hdr"]}"\n')
        T = nm["table"]
        open(os.path.join(RH, nm["hooks"]), "w").write(
            f"/* {nm['hooks']} — {nm['what']}, partition '{p}' (src/{st}.c) vs retail, on identical state\n"
            " * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported\n"
            f" * function: HOOK_V({fn}_<va>, N, {fn}_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a\n"
            " * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail\n"
            " * with itself). */\n"
            f'#include "rh_port.h"\n#include "{nm["hdr"]}"\n\n'
            f"#define RH_TABLE {T}\nextern hook_t {T}[];\n"
            f"/* row indices (must follow the table's order) */\nenum {{ X_{fn}_none_ }};\n\n"
            f"hook_t {T}[] = {{\n    {{ 0 }},   /* (placeholder: replace with the first row) */\n}};\n"
            f"const int {T}_N = 0;\n")
        print(f"{p}: {len(vs)} functions")


if __name__ == "__main__":
    if "--part-globals" in sys.argv:
        gen_globals(sys.argv[sys.argv.index("--part-globals") + 1])
        sys.exit(0)
    if "--new-parts" in sys.argv:   # only partitions without files yet (the globals stay as they are: agents use them)
        gen_parts()
        sys.exit(0)
    gen_globals()
    if "--globals-only" not in sys.argv:
        gen_parts()
